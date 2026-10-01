/**
 * @file if_net.c
 * @brief 联网服务 —— **M1 最小实现**。
 *
 * M1 只要求一件事：USART3 的 DMA + 空闲中断收得到字节且不丢。
 * 所以这里做的事只有三件：
 *   1. if_net_init 把 USART3 的接收拉起来（真正的搬运在 drv_uart 里）；
 *   2. if_net_poll 定期把收到的字节取走、计数（现在是丢弃，因为还没有 AT 状态机）；
 *   3. 对外一律报"离线"，publish 一律返回 CM_ERR_STATE。
 *
 * **M4 要在这里填的东西**（届时不要改这个文件的对外接口，只填内部）：
 *   - AT 能力探测、WiFi 连接、MQTT CONNECT/SUB 的状态机与超时重试
 *   - URC 解析（+MQTTDISCONNECTED / +MQTTSUBRECV / WIFI DISCONNECT）
 *   - 收到下行报文时回调 s_rx_cb；publish 走 drv_uart_net_write
 */

#include "if_net.h"

#include "drv_uart.h"
#include "cm_log.h"

/** 一次 poll 最多搬多少字节（超过就留到下一次，避免长时间占用 NetTask） */
#define IF_NET_POLL_CHUNK   64u

static if_net_cfg_t s_cfg;
static bool         s_inited  = false;

static if_net_state_e s_state = IF_NET_ST_RESET;

static uint32_t s_rx_bytes   = 0u;
static uint32_t s_rx_dropped = 0u;

static uint8_t  s_drain_buf[IF_NET_POLL_CHUNK];

static const char *const s_state_name[] = {
    [IF_NET_ST_RESET]        = "RESET",
    [IF_NET_ST_AT_PROBE]     = "AT_PROBE",
    [IF_NET_ST_WIFI]         = "WIFI",
    [IF_NET_ST_MQTT_CONNECT] = "MQTT_CONNECT",
    [IF_NET_ST_MQTT_SUB]     = "MQTT_SUB",
    [IF_NET_ST_ONLINE]       = "ONLINE",
    [IF_NET_ST_RECONNECT]    = "RECONNECT",
    [IF_NET_ST_FATAL]        = "FATAL",
};

cm_err_t if_net_init(const if_net_cfg_t *cfg, UART_HandleTypeDef *huart)
{
    if ((cfg == NULL) || (huart == NULL)) {
        return CM_ERR_PARAM;
    }

    s_cfg        = *cfg;
    s_state      = IF_NET_ST_RESET;      /* M1：停在初始态，永远离线 */
    s_rx_bytes   = 0u;
    s_rx_dropped = 0u;
    s_inited     = true;

    if (drv_uart_net_init(huart) != CM_OK) {
        return CM_ERR_IO;
    }

    /* 打一行参数：换 broker/换主题时，开机就能确认宏有没有真的生效 */
    cm_log_printf("net: broker=%s port=%u up=%s down=%s",
                  s_cfg.broker, (unsigned)s_cfg.port, s_cfg.topic_up, s_cfg.topic_down);
    return CM_OK;
}

void if_net_set_downlink_cb(if_net_rx_fn cb, void *user)
{
    /* M1 阶段还没有报文解析：收到的是原始字节流，没法判断"一条报文"到哪结束。
     * 所以这里先不登记回调（登记了也没人触发），等 M4 接上 AT 状态机与 URC 解析后
     * 再把回调接进解析结果里。接口签名保持不变，上层代码不用改。 */
    CM_UNUSED(cb);
    CM_UNUSED(user);
}

void if_net_poll(uint32_t now_ms)
{
    uint16_t got;

    CM_UNUSED(now_ms);

    if (!s_inited) {
        return;
    }

    /* 兜底搬一次：中断已经搬过，这里防漏中断 */
    drv_uart_net_poll();

    /* 取走并计数。M4 会把这里换成"喂给 AT 状态机" */
    got = drv_uart_net_read(s_drain_buf, IF_NET_POLL_CHUNK);
    while (got > 0u) {
        s_rx_bytes += got;
        got = drv_uart_net_read(s_drain_buf, IF_NET_POLL_CHUNK);
    }

    s_rx_dropped = drv_uart_net_rx_dropped();
}

bool if_net_can_accept(void)
{
    /* M1 没有在途命令这回事；M4 再按"同一时刻只允许一条 AT 命令"实现 */
    return false;
}

cm_err_t if_net_mqtt_publish(const char *topic, const char *payload, bool retain)
{
    CM_UNUSED(topic);
    CM_UNUSED(payload);
    CM_UNUSED(retain);

    /* M1 永远离线：如实回报状态错误，让上层去累加 err_cnt，而不是假装发成功 */
    return CM_ERR_STATE;
}

if_net_state_e if_net_state(void)
{
    return s_state;
}

bool if_net_online(void)
{
    return (s_state == IF_NET_ST_ONLINE);
}

const char *if_net_state_str(if_net_state_e st)
{
    if ((uint8_t)st >= (uint8_t)(sizeof(s_state_name) / sizeof(s_state_name[0]))) {
        return "?";
    }
    return (s_state_name[st] != NULL) ? s_state_name[st] : "?";
}

bool if_net_probe_done(void)
{
    return false;       /* M4 才探测模组能力 */
}

bool if_net_mqtt_capable(void)
{
    return false;
}

uint32_t if_net_err_cnt(void)
{
    return 0u;
}

uint32_t if_net_rx_bytes(void)
{
    return s_rx_bytes;
}

uint32_t if_net_rx_dropped(void)
{
    return s_rx_dropped;
}
