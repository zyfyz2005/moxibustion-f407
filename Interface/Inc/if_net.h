/**
 * @file if_net.h
 * @brief 联网服务：AT 指令 + MQTT 会话（ESP8266 走 USART3）。
 *
 * 分工：本模块只管**传输**——AT 命令序列、WiFi/MQTT 会话、URC 解析、超时重试、重连；
 * 报文语义（遥测 JSON 怎么拼、下行指令什么意思）属于 Application/app_protocol.c。
 * 这样换云平台只改 app_cfg.h 的宏和本模块，6 个任务一行不动。
 *
 * 三条硬约束（计划书第五节）：
 *   1. 禁止阻塞式 AT 收发（不许 HAL_UART_Receive(..., 2000)），用 DMA + 空闲中断 + 环形缓冲；
 *   2. 同一时刻只允许一条在途 AT 命令，不做命令交错、不做响应归属匹配；
 *   3. 等待响应期间必须同时解析 URC（+MQTTDISCONNECTED / +MQTTSUBRECV / WIFI DISCONNECT）。
 *
 * 时间由调用方传入：本模块不取时间、不碰 FreeRTOS 对象，便于离线编译与后续单测。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/** 会话状态 */
typedef enum {
    IF_NET_ST_RESET = 0,       /**< 上电初始，等待第一次 poll 启动流程 */
    IF_NET_ST_AT_PROBE,        /**< AT 能力探测：是否支持 AT+MQTT* 指令 */
    IF_NET_ST_WIFI,            /**< 连 AP */
    IF_NET_ST_MQTT_CONNECT,    /**< MQTT CONNECT（带 LWT） */
    IF_NET_ST_MQTT_SUB,        /**< 订阅下行主题 */
    IF_NET_ST_ONLINE,          /**< 在线，可发布 */
    IF_NET_ST_RECONNECT,       /**< 断开后等待重连 */
    IF_NET_ST_FATAL,           /**< 模组不支持 MQTT AT 指令：换模组才能继续 */
} if_net_state_e;

/** 连接参数（由 Application 从 app_cfg.h 的宏组装进来） */
typedef struct {
    const char *broker;        /**< MQTT broker 域名 */
    uint16_t    port;          /**< MQTT 端口，一般 1883 */
    const char *client_id;     /**< MQTT client id，需全局唯一 */
    const char *topic_up;      /**< 上行主题：遥测 / ACK / 状态；同时用作遗嘱主题 */
    const char *topic_down;    /**< 下行订阅主题 */
    const char *lwt_payload;   /**< 遗嘱载荷，例如 {"type":"status","online":false} */
    const char *ssid;          /**< WiFi 名称 */
    const char *password;      /**< WiFi 密码 */
} if_net_cfg_t;

/** 收到下行报文时的回调（在 if_net_poll 的调用者上下文里执行） */
typedef void (*if_net_rx_fn)(const char *payload, uint16_t len, void *user);

/**
 * @brief 绑定参数、初始化 USART3 的 DMA + 空闲中断接收，并复位状态机
 * @param cfg   连接参数
 * @param huart CubeMX 生成的 USART3 句柄（它的 hdmarx/hdmatx 已由 MspInit 链接好）
 * @return CM_OK；CM_ERR_PARAM 参数为空；CM_ERR_IO 启动 DMA 接收失败
 * @note **M1 阶段这里只做到"收得到字节不丢"**：状态机停在 IF_NET_ST_RESET，
 *       永远离线。AT 命令状态机、WiFi/MQTT 会话是 M4 的活，会填在 if_net_poll 里
 */
cm_err_t if_net_init(const if_net_cfg_t *cfg, UART_HandleTypeDef *huart);

/**
 * @brief 注册下行报文回调
 * @param cb   回调函数；传 NULL 表示取消注册
 * @param user 回调时原样透传的用户指针
 * @note **M1 阶段只保留接口、不做实际分发**：这时收到的是原始字节流，判不出报文边界；
 *       M4 接上 AT 状态机与 URC 解析后才真正回调。接口签名不变，上层不用跟着改
 * @note 回调在 if_net_poll 的调用者上下文里执行，也就是 NetTask 里，
 *       所以回调里可以做入队这类非阻塞操作，但不要长时间阻塞
 */
void if_net_set_downlink_cb(if_net_rx_fn cb, void *user);

/**
 * @brief 推进状态机：扫描接收缓冲、解析 URC、判超时、按需重连
 * @param now_ms 当前毫秒时基
 * @note NetTask 每 200 ms 调一次；内部所有等待都是"分片推进"，不会阻塞
 */
void if_net_poll(uint32_t now_ms);

/** @brief 是否可以接受新的发布请求（false = 有 AT 命令在途） */
bool if_net_can_accept(void);

/**
 * @brief 发布一条 MQTT 报文
 * @param retain true = 保留消息（在线状态用），false = 普通发布
 * @note 非阻塞：接受即返回 CM_OK（真正发完由后续 if_net_poll 推进）；
 *       有命令在途返回 CM_ERR_BUSY；未上线返回 CM_ERR_STATE
 */
cm_err_t if_net_mqtt_publish(const char *topic, const char *payload, bool retain);

/** @brief 当前状态 */
if_net_state_e if_net_state(void);

/** @brief 是否已上线（可发布） */
bool if_net_online(void);

/** @brief 状态名（日志用，返回静态字符串） */
const char *if_net_state_str(if_net_state_e st);

/** @brief AT 能力探测是否已完成 */
bool if_net_probe_done(void);

/** @brief 模组是否支持 MQTT AT 指令（探测结果） */
bool if_net_mqtt_capable(void);

/** @brief 错误计数：AT 超时/失败、MQTT 连接失败、URC 解析失败等累加 */
uint32_t if_net_err_cnt(void);

/**
 * @brief 累计从模组口收到的字节数
 * @note M1 出口自检"USART3 的 DMA + 空闲中断收到字节不丢"就看这个数：
 *       往模组口发数据，这个计数应该跟着涨，且丢字节计数保持 0
 */
uint32_t if_net_rx_bytes(void);

/** @brief 累计被丢弃的字节数（环形缓冲满导致）；正常应为 0 */
uint32_t if_net_rx_dropped(void);
