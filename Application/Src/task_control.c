/**
 * @file task_control.c
 * @brief ControlTask（优先级 5，栈 512 word）
 *
 * 职责：按键消抖、状态机、倒计时、阈值判断、指令校验、改参数、
 *       ACK 生成、秒计数、堆占用打印。
 *
 * 两条设计约束：
 *   1. **全程不阻塞**。改阈值/时长是纯内存操作；删掉 24C02 持久化之后，
 *      那条异步写路径（发请求、等回执、300ms 超时）整条消失了，
 *      所以改参数期间按键和新指令照常响应。
 *   2. **只在本地状态上改，改完一次性提交**。g_moxa_state 的唯一写入者是本任务，
 *      提交时自增 seq 并广播事件位。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_fsm.h"
#include "app_protocol.h"
#include "app_heartbeat.h"

#include "cm_time.h"
#include "cm_log.h"

#include "if_input.h"
#include "if_sensor.h"

/*============================ 小工具 ============================*/

/**
 * @brief 生成并投递一条回执
 * @note 带在里面的 state 是"处理这条指令那一刻的状态"，不是发送时的状态。
 *       ACK 进了 at_tx_queue 还可能排队等一会儿，发送时再刷新就会与 msg_id 对不上。
 */
static void ctrl_ack_emit(MoxaState_t *state, uint16_t msg_id, uint8_t cmd, uint8_t code)
{
    AckMsg_t ack;

    ack.msg_id = msg_id;
    ack.cmd    = cmd;
    ack.code   = code;
    ack.state  = state->state;

    if (xQueueSend(q_ack, &ack, 0) == pdTRUE) {
        return;
    }

    /* 队列满：丢队头最旧的一条腾一格。单生产者单线程，丢完必定能入队，
     * 所以不重试、更不阻塞（计划书第二节 ack_queue 的满策略） */
    {
        AckMsg_t dropped;
        if (xQueueReceive(q_ack, &dropped, 0) == pdTRUE) {
            app_err_inc(APP_ERR_QDROP);
        }
    }
    if (xQueueSend(q_ack, &ack, 0) != pdTRUE) {
        app_err_inc(APP_ERR_QDROP);     /* 理论到不了 */
    }
}

/** 判断状态是否有实质变化（忽略 seq 本身，也避开结构体填充字节的比较陷阱） */
static bool ctrl_state_changed(const MoxaState_t *previous, const MoxaState_t *current)
{
    return (previous->state           != current->state)           ||
           (previous->muted           != current->muted)           ||
           (previous->switch_on       != current->switch_on)       ||
           (previous->temp_valid      != current->temp_valid)      ||
           (previous->temp_c          != current->temp_c)          ||
           (previous->humi_pct        != current->humi_pct)        ||
           (previous->remain_s        != current->remain_s)        ||
           (previous->th_max_c        != current->th_max_c)        ||
           (previous->duration_s      != current->duration_s)      ||
           (previous->temp_alarm      != current->temp_alarm)      ||
           (previous->countdown_alarm != current->countdown_alarm) ||
           (previous->uptime_s        != current->uptime_s)        ||
           (previous->err_cnt         != current->err_cnt);
}

/*============================ 指令处理 ============================*/

static void ctrl_handle_cmd(MoxaState_t *state, const CmdMsg_t *msg)
{
    const int32_t value = msg->value;

    /* 判定顺序是"先 cmd 后 value"（计划书第二节配置写路径）：
     * 未知指令即使 value 越界也回 code=1，而不是 code=2 */
    switch ((moxa_cmd_e)msg->cmd) {
    case MOXA_CMD_START:
        app_fsm_start(state);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;

    case MOXA_CMD_STOP:
        app_fsm_stop(state);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;

    case MOXA_CMD_RESET:
        app_fsm_reset(state);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;

    case MOXA_CMD_MUTE:
        /* 非 ALARM 状态下静音键无效果，但指令本身合法，照样回 ok */
        app_fsm_mute_key(state);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;

    case MOXA_CMD_QUERY:
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;

    case MOXA_CMD_SET_TH:
    case MOXA_CMD_SET_DUR: {
        const bool    is_threshold = (msg->cmd == (uint8_t)MOXA_CMD_SET_TH);
        const int32_t min_value    = is_threshold ? (int32_t)APP_TH_MIN_C  : (int32_t)APP_DUR_MIN_S;
        const int32_t max_value    = is_threshold ? (int32_t)APP_TH_MAX_C  : (int32_t)APP_DUR_MAX_S;

        if ((value < min_value) || (value > max_value)) {
            app_err_inc(APP_ERR_BADCMD);
            ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_BAD_PARAM);
            break;
        }
        /* 合法：直接改内存、立即生效。
         * 删掉 24C02 持久化之后就没有"异步写 + 等回执"了，所以
         * code=3 busy（配置在途）与 code=5（配置读写失败）这两条路径都不再产生 */
        if (is_threshold) {
            app_fsm_set_threshold(state, (uint16_t)value);
        } else {
            app_fsm_set_duration(state, (uint32_t)value);
        }
        cm_log_printf("cfg: set %s=%ld", app_cmd_str(msg->cmd), (long)value);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_OK);
        break;
    }

    default:
        app_err_inc(APP_ERR_BADCMD);
        ctrl_ack_emit(state, msg->msg_id, msg->cmd, (uint8_t)MOXA_CODE_UNKNOWN_CMD);
        break;
    }
}

/*============================ 按键 ============================*/

static void ctrl_handle_keys(MoxaState_t *state, uint32_t now)
{
    const uint32_t mask = if_input_poll(now);

    if ((mask & IF_INPUT_KEY_POWER) != 0u) {
        app_fsm_power_key(state);
        cm_log_printf("key: power -> %s", app_state_str(state->state));
    }
    if ((mask & IF_INPUT_KEY_MUTE) != 0u) {
        app_fsm_mute_key(state);
        cm_log_printf("key: mute (muted=%u)", (unsigned)state->muted);
    }
    if ((mask & IF_INPUT_KEY_RESET) != 0u) {
        app_fsm_reset(state);
        cm_log_printf("key: reset");
    }
}

/*============================ 诊断打印 ============================*/

static void ctrl_print_diag(const MoxaState_t *state, uint32_t now, uint32_t *last_ms)
{
    if (!cm_timeout_expired(now, *last_ms, APP_HEAP_PRINT_PERIOD_MS)) {
        return;
    }
    *last_ms = now;

    cm_log_printf("diag: heap=%u min=%u state=%s up=%us err=%u temp=%d th=%u dur=%u",
                  (unsigned)xPortGetFreeHeapSize(),
                  (unsigned)xPortGetMinimumEverFreeHeapSize(),
                  app_state_str(state->state),
                  (unsigned)state->uptime_s,
                  (unsigned)state->err_cnt,
                  (int)state->temp_c,
                  (unsigned)state->th_max_c,
                  (unsigned)state->duration_s);

#if (APP_STACK_MONITOR_ENABLE == 1)
    {
        uint8_t task_count = 0u;
        const app_task_info_t *info = app_tasks_info(&task_count);

        for (uint8_t i = 0u; i < task_count; i++) {
            if ((info != NULL) && (info[i].handle != NULL)) {
                /* 参数是剩余栈的**最小值**（word），只会越来越小；
                 * 调栈深就以这个数为依据，别凭感觉 */
                cm_log_printf("stack free(w): %s=%u",
                              info[i].name,
                              (unsigned)uxTaskGetStackHighWaterMark(info[i].handle));
            }
        }
    }
#endif
}

static void ctrl_print_sensor_stat(uint32_t now, uint32_t *last_ms)
{
    const if_sensor_stat_t *ss;
    uint32_t rate;      /* 百分之一的百分之一，即 0.01% 为单位 */

    if (!cm_timeout_expired(now, *last_ms, APP_STAT_PRINT_PERIOD_MS)) {
        return;
    }
    *last_ms = now;

    ss = if_sensor_get_stat();
    if (ss == NULL) {
        return;
    }
    rate = (ss->total == 0u) ? 0u : ((ss->fail * 10000u) / ss->total);
    cm_log_printf("dht11: total=%u fail=%u rate=%u.%02u%% max_consec=%u",
                  (unsigned)ss->total,
                  (unsigned)ss->fail,
                  (unsigned)(rate / 100u),
                  (unsigned)(rate % 100u),
                  (unsigned)ss->max_consec);
}

/*============================ 任务本体 ============================*/

void task_control(void *arg)
{
    MoxaState_t      state;
    MoxaState_t      prev_state;
    cm_sec_counter_t uptime_counter;
    TickType_t       last_wake;
    uint32_t         last_diag_ms;
    uint32_t         last_stat_ms;
    uint32_t         last_now_ms;

    CM_UNUSED(arg);

    (void)if_input_init(APP_KEY_DEBOUNCE_MS);

    app_state_take(&state);
    prev_state = state;
    cm_sec_counter_reset(&uptime_counter);

    last_wake = xTaskGetTickCount();
    last_diag_ms = app_now_ms();
    last_stat_ms = last_diag_ms;
    last_now_ms  = last_diag_ms;

    for (;;) {
        uint32_t now = app_now_ms();
        uint32_t delta_ms = cm_elapsed_ms(now, last_now_ms);
        last_now_ms = now;

        /* 1. 按键：30ms 消抖由 if_input 负责，这里只处理按下沿 */
        ctrl_handle_keys(&state, now);

        /* 2. 传感器采样：全部取出，最后一份生效（丢弃中间过期的） */
        {
            SensorSample_t sample;
            while (xQueueReceive(q_sensor, &sample, 0) == pdTRUE) {
                app_fsm_set_temp(&state, sample.valid != 0u, sample.temp_c, sample.humi_pct);
            }
        }

        /* 3. 下行指令：全部取出逐个处理 */
        {
            CmdMsg_t msg;
            while (xQueueReceive(q_cmd, &msg, 0) == pdTRUE) {
                ctrl_handle_cmd(&state, &msg);
            }
        }

        /* 4. 报警条件与状态迁移 */
        app_fsm_eval(&state);

        /* 5. 秒计数：倒计时递减 + uptime 各走一份，互不干扰。
         *    用实际时间差而不是写死的 10ms，免得任务被抢占时秒数走慢 */
        if (cm_sec_counter_add(&uptime_counter, delta_ms)) {
            app_fsm_tick_1s(&state);
            state.uptime_s++;
            app_uptime_set(state.uptime_s);     /* 给日志时间戳用的镜像 */
        }

        /* 6. 全系统错误计数汇总（各任务分类累加，这里合并进状态） */
        state.err_cnt = app_err_total();

        /* 7. 有实质变化才提交并广播，避免每秒上百次无谓唤醒 */
        if (ctrl_state_changed(&prev_state, &state)) {
            app_state_commit(&state);
            prev_state = state;
        }

        /* 8. 诊断打印 */
        ctrl_print_diag(&state, now, &last_diag_ms);
        ctrl_print_sensor_stat(now, &last_stat_ms);

        /* 9. 心跳：本任务期限 100ms，每轮 10ms 推进一次 */
        app_hb_kick(APP_HB_CTRL, app_now_ms());

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_CTRL_PERIOD_MS));
    }
}
