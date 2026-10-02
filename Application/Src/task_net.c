/**
 * @file task_net.c
 * @brief NetTask（优先级 2，栈 1024 word）
 *
 * 职责：驱动 AT 状态机（if_net），并把"待发的东西"翻译成 MQTT 报文发出去。
 * 每 200ms 一轮，一轮做四件事（计划书第五节 NetTask 主循环）：
 *   1. 推进 AT 状态机；
 *   2. 没有在途命令就发一条待发（at_tx_queue）；
 *   3. 把 ControlTask 生成的 ACK 从 ack_queue 搬进 at_tx_queue；
 *   4. 每 3s 补一条遥测。
 *
 * 三条容易踩的地方，都在代码里就地标了：
 *   - 遥测必须先取状态快照、**释放锁之后**再拼 JSON；
 *   - ACK 的 state 字段是"生成时刻"的，搬运时原样带回，不在发送时刷新；
 *   - NetTask 自己产生的 ACK（入队失败回 busy、报文格式错回 format_err）
 *     是 ACK 路径上仅有的两处例外，发布者始终只有 NetTask。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_protocol.h"
#include "app_heartbeat.h"

#include "cm_time.h"
#include "cm_log.h"

#include "if_net.h"

#include "usart.h"      /* huart3：模组串口句柄（CubeMX 生成） */

/* 静态缓冲：计划书明确要求 NetTask 的 JSON / AT 缓冲一律静态分配，不放栈上 */
static char s_tele_buf[APP_TELE_PAYLOAD_MAX];
static char s_ack_buf[APP_ACK_PAYLOAD_MAX];
static char s_status_buf[APP_STATUS_PAYLOAD_MAX];

/* at_tx_queue 的重建暂存区。
 * 这个队列的生产者和消费者都是本任务自己，所以"整体取出→删一条→原序放回"
 * 不存在并发问题，不需要加锁（换成两个任务访问就必须改写这里） */
static AtTxItem_t s_rebuild_buf[APP_Q_AT_TX_LEN];

/* 从队列取出、正在等待发送的当前条目 */
static AtTxItem_t s_current;
static bool       s_current_valid;

static if_net_cfg_t s_cfg;

/* 上次上报过的在线状态：-1 = 还没上报过 */
static int8_t s_last_online = -1;

/*============================ 待发队列 ============================*/

/**
 * @brief 往待发队列放一条
 * @return true = 放进去或成功腾了位置；false = 队里全是 ACK，新条目被丢弃
 * @note 满策略（计划书第二节）：优先丢最旧的一条 TELEMETRY；没有遥测项就丢新条目
 */
static bool net_at_tx_push(const AtTxItem_t *item)
{
    uint8_t count      = 0u;
    int     drop_index = -1;

    if (xQueueSend(q_at_tx, item, 0) == pdTRUE) {
        return true;
    }

    while ((count < (uint8_t)APP_Q_AT_TX_LEN) &&
           (xQueueReceive(q_at_tx, &s_rebuild_buf[count], 0) == pdTRUE)) {
        count++;
    }

    /* 找最旧的一条遥测 */
    for (uint8_t i = 0u; i < count; i++) {
        if (s_rebuild_buf[i].kind == (uint8_t)APP_AT_TX_TELEMETRY) {
            drop_index = (int)i;
            break;
        }
    }

    if (drop_index >= 0) {
        for (uint8_t i = (uint8_t)drop_index; (i + 1u) < count; i++) {
            s_rebuild_buf[i] = s_rebuild_buf[i + 1u];
        }
        count--;
    }

    for (uint8_t i = 0u; i < count; i++) {
        (void)xQueueSend(q_at_tx, &s_rebuild_buf[i], 0);
    }

    if (drop_index >= 0) {
        (void)xQueueSend(q_at_tx, item, 0);
        app_err_inc(APP_ERR_QDROP);
        return true;
    }

    app_err_inc(APP_ERR_QDROP);
    return false;
}

static void net_push_telemetry(void)
{
    AtTxItem_t item;

    item.kind   = (uint8_t)APP_AT_TX_TELEMETRY;
    item.msg_id = 0u;
    item.code   = 0u;
    item.cmd    = 0u;
    item.state  = 0u;
    (void)net_at_tx_push(&item);
}

/**
 * @brief NetTask 自己生成一条 ACK
 * @note 只在"指令没能到达 ControlTask"这两种情况下用（入队失败 / 报文格式错）。
 *       state 取生成时刻的快照
 */
static void net_local_ack(uint16_t msg_id, uint8_t cmd, uint8_t code)
{
    MoxaState_t snap;
    AtTxItem_t  item;

    app_state_take(&snap);

    item.kind   = (uint8_t)APP_AT_TX_ACK;
    item.msg_id = msg_id;
    item.cmd    = cmd;
    item.code   = code;
    item.state  = snap.state;
    (void)net_at_tx_push(&item);
}

/*============================ 下行 ============================*/

static void net_on_downlink(const char *payload, uint16_t len, void *user)
{
    app_parsed_cmd_t parsed;

    CM_UNUSED(user);

    parsed = app_proto_parse_cmd(payload, (size_t)len, app_now_ms());

    /* 缺 msg_id：没法回一条能被对上的 ACK，只能回 msg_id=0 的格式错 */
    if (parsed.result == APP_PARSE_ERR_NO_MSGID) {
        app_err_inc(APP_ERR_NO_MSGID);
        net_local_ack(0u, (uint8_t)MOXA_CMD_UNKNOWN, (uint8_t)MOXA_CODE_FORMAT_ERR);
        return;
    }
    if (parsed.result != APP_PARSE_OK) {
        app_err_inc(APP_ERR_JSON);
        net_local_ack(0u, (uint8_t)MOXA_CMD_UNKNOWN, (uint8_t)MOXA_CODE_FORMAT_ERR);
        return;
    }

    if (xQueueSend(q_cmd, &parsed.cmd, 0) != pdTRUE) {
        /* cmd_queue 满：不阻塞，直接回忙（ACK 路径的第二处例外） */
        app_err_inc(APP_ERR_QDROP);
        net_local_ack(parsed.cmd.msg_id, parsed.cmd.cmd, (uint8_t)MOXA_CODE_BUSY);
    }
}

/*============================ 发送 ============================*/

/** 把 ack_queue 里的回执搬进待发队列 */
static void net_drain_ack(void)
{
    AckMsg_t ack;
    uint8_t  drain_count = 0u;

    while ((drain_count < (uint8_t)APP_Q_ACK_LEN) && (xQueueReceive(q_ack, &ack, 0) == pdTRUE)) {
        AtTxItem_t item;

        drain_count++;
        item.kind   = (uint8_t)APP_AT_TX_ACK;
        item.msg_id = ack.msg_id;
        item.cmd    = ack.cmd;
        item.code   = ack.code;
        item.state  = ack.state;    /* 生成时刻的状态，原样带走 */
        (void)net_at_tx_push(&item);
    }
}

/** 没有在途 AT 命令时，把当前条目组装成 JSON 发出去 */
static void net_pump_tx(void)
{
    const char *payload;
    cm_err_t    result;

    if (!if_net_online()) {
        /* 没上线就不取条目：取了也只能丢，白扔队列里的东西 */
        return;
    }
    if (!if_net_can_accept()) {
        return;                 /* 有命令在途，等下一轮 */
    }

    if (!s_current_valid) {
        if (xQueueReceive(q_at_tx, &s_current, 0) != pdTRUE) {
            return;
        }
        s_current_valid = true;
    }

    if (s_current.kind == (uint8_t)APP_AT_TX_TELEMETRY) {
        MoxaState_t snap;

        /* 先取快照、释放锁，再拼 JSON。反之可能读到 ControlTask 写到一半的状态
         * （比如 state 已更新、remain_s 还是旧值），上报出自相矛盾的组合 */
        app_state_take(&snap);
        if (app_proto_build_telemetry(&snap, s_tele_buf, sizeof(s_tele_buf)) != CM_OK) {
            app_err_inc(APP_ERR_JSON);
            s_current_valid = false;
            return;
        }
        payload = s_tele_buf;
    } else {
        AckMsg_t ack;

        ack.msg_id = s_current.msg_id;
        ack.cmd    = s_current.cmd;
        ack.code   = s_current.code;
        ack.state  = s_current.state;
        if (app_proto_build_ack(&ack, s_ack_buf, sizeof(s_ack_buf)) != CM_OK) {
            app_err_inc(APP_ERR_JSON);
            s_current_valid = false;
            return;
        }
        payload = s_ack_buf;
    }

    result = if_net_mqtt_publish(MQTT_TOPIC_UP, payload, false);
    if (result == CM_OK) {
        s_current_valid = false;
    } else if (result == CM_ERR_BUSY) {
        /* 条目留在手里，下一轮再发 */
    } else {
        app_err_inc(APP_ERR_MQTT);
        s_current_valid = false;
    }
}

/** 在线状态变化时发一条 retained 状态报文 */
static void net_report_status(void)
{
    int8_t   current_online = if_net_online() ? 1 : 0;
    cm_err_t result;

    if (current_online == s_last_online) {
        return;
    }
    if (!if_net_can_accept()) {
        return;
    }
    if (app_proto_build_status(current_online != 0, s_status_buf, sizeof(s_status_buf)) != CM_OK) {
        app_err_inc(APP_ERR_JSON);
        return;
    }

    result = if_net_mqtt_publish(MQTT_TOPIC_UP, s_status_buf, true);
    if (result == CM_OK) {
        s_last_online = current_online;
        cm_log_printf("net: status online=%d", (int)current_online);
    } else if (result != CM_ERR_BUSY) {
        app_err_inc(APP_ERR_MQTT);
    }
}

/*============================ 任务本体 ============================*/

static void net_cfg_init(void)
{
    s_cfg.broker      = MQTT_BROKER;
    s_cfg.port        = (uint16_t)MQTT_PORT;
    s_cfg.client_id   = MQTT_CLIENT_ID;
    s_cfg.topic_up    = MQTT_TOPIC_UP;
    s_cfg.topic_down  = MQTT_TOPIC_DOWN;
    s_cfg.lwt_payload = MQTT_LWT_PAYLOAD;
    s_cfg.ssid        = WIFI_SSID;
    s_cfg.password    = WIFI_PASSWORD;
}

void task_net(void *arg)
{
    TickType_t     last_wake = xTaskGetTickCount();
    uint32_t       last_tele;
    uint32_t       last_stat_ms;
    if_net_state_e last_st   = IF_NET_ST_RESET;

    CM_UNUSED(arg);

    s_current_valid = false;

    net_cfg_init();
    if (if_net_init(&s_cfg, &huart3) != CM_OK) {
        app_err_inc(APP_ERR_AT);
        cm_log_printf("net: init failed");
    }
    if_net_set_downlink_cb(net_on_downlink, NULL);

    last_tele = app_now_ms();
    last_stat_ms = last_tele;

    for (;;) {
        uint32_t now = app_now_ms();

        /* 1. 推进 AT 状态机：扫描接收缓冲、解析 URC、判超时、按需重连 */
        if_net_poll(now);

        /* 2. 没有在途命令就送一条 */
        net_pump_tx();

        /* 3. 收 ControlTask 的 ACK */
        net_drain_ack();

        /* 4. 在线状态变化就上报 */
        net_report_status();

        /* 5. 每 3s 一条遥测——但**只在已上线时排**。
         *    离线时排进去也发不出去，队列（深度 8）填满后每塞一条就丢一条最旧的，
         *    还顺带把 err_cnt 一路推高（每 30s 涨 10）——那是噪音，不是真错误。 */
        if (cm_timeout_expired(now, last_tele, APP_TELEMETRY_PERIOD_MS)) {
            last_tele = now;
            if (if_net_online()) {
                net_push_telemetry();
            }
        }

        /* 6. 状态变化打一行日志，方便对着 M4 出口条件看流程走到哪一步 */
        if (if_net_state() != last_st) {
            last_st = if_net_state();
            cm_log_printf("net: %s err=%u",
                          if_net_state_str(last_st),
                          (unsigned)if_net_err_cnt());
        }

        /* 7. 心跳：等 AT 响应的过程中也在推进，期限 2s */
        if (cm_timeout_expired(now, last_stat_ms, APP_NET_STAT_PERIOD_MS)) {
            last_stat_ms = now;
            /* M1 阶段联网自检就看这一行：往模组口发数据时 rx 应该跟着涨、drop 保持 0 */
            cm_log_printf("net: %s rx=%u B drop=%u",
                          if_net_state_str(if_net_state()),
                          (unsigned)if_net_rx_bytes(),
                          (unsigned)if_net_rx_dropped());
        }

        app_hb_kick(APP_HB_NET, app_now_ms());

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_NET_SLICE_MS));
    }
}
