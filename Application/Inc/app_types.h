/**
 * @file app_types.h
 * @brief 应用层五种通信结构体 + 状态/指令/错误码枚举。
 *        字段与计划书第二节"接口定义"逐项对应；有两处是本实现补的，已就地注明原因。
 */

#pragma once

#include "cm_types.h"

/*============================ 运行时配置 ============================*/

/**
 * 运行时要用的两个可调值。
 * 原本定义在 Interface 层的 `if_store.h` 里（那时它是"要落盘的东西"）；
 * 删掉 24C02 持久化之后，它只剩"内存里的配置载体"这一个身份，所以搬到应用层。
 */
typedef struct {
    uint16_t th_max_c;      /**< 高温阈值，单位 ℃，范围 [APP_TH_MIN_C, APP_TH_MAX_C] */
    uint32_t duration_s;    /**< 倒计时时长，单位 s，范围 [APP_DUR_MIN_S, APP_DUR_MAX_S] */
} moxa_cfg_t;

/*============================ 状态机 ============================*/

/** 设备状态。muted 与 state 正交，不作为状态节点（计划书第四节"MUTED 闭环"） */
typedef enum {
    MOXA_ST_WAITING   = 0,
    MOXA_ST_COUNTDOWN = 1,
    MOXA_ST_ALARM     = 2,
} moxa_state_e;

/** 共享状态。state_mutex 保护，ControlTask 是唯一写入者 */
typedef struct {
    uint8_t   state;            /**< moxa_state_e */
    uint8_t   muted;            /**< 静音标志，与 state 正交 */
    uint8_t   switch_on;        /**< 是否已开机（WAITING 下为 0） */
    uint8_t   temp_valid;       /**< 温度是否有效（读失败时为 0） */
    int16_t   temp_c;           /**< 温度，整数 ℃ */
    uint16_t  humi_pct;         /**< 湿度，整数 % */
    uint32_t  remain_s;         /**< 倒计时剩余秒 */
    uint16_t  th_max_c;         /**< 高温阈值 */
    uint32_t  duration_s;       /**< 倒计时时长 */
    uint8_t   temp_alarm;       /**< 温度报警条件成立 */
    uint8_t   countdown_alarm;  /**< 倒计时归零 */
    uint32_t  uptime_s;         /**< 独立秒计数器（不用 HAL_GetTick()/1000） */
    uint16_t  err_cnt;          /**< 错误计数（内存里，不落盘） */
    uint32_t  seq;              /**< 每次变更自增，消费者据此判断有无新数据 */
} MoxaState_t;

/*============================ 指令与回执 ============================*/

/** 支持的 7 种指令。枚举值就是 CmdMsg_t.cmd 的取值 */
typedef enum {
    /**
     * 无法识别的指令名占位值。
     * 为什么用 0 而不是在 NetTask 直接回错误：计划书要求判定顺序是"先 cmd 后 value"，
     * 未知指令要回 code=1（而不是格式错 code=4），这个判定必须发生在 ControlTask 里。
     * 所以 NetTask 把不认识的名字映射成 0 照样入队，由 ControlTask 统一判出 code=1。
     */
    MOXA_CMD_UNKNOWN    = 0,
    MOXA_CMD_START      = 1,    /**< 开机 */
    MOXA_CMD_STOP       = 2,    /**< 关机 */
    MOXA_CMD_RESET      = 3,    /**< 复位回 WAITING */
    MOXA_CMD_MUTE       = 4,    /**< 静音 */
    MOXA_CMD_QUERY      = 5,    /**< 查询（只回 ACK，不改变状态） */
    MOXA_CMD_SET_TH     = 6,    /**< 设阈值，value = ℃ */
    MOXA_CMD_SET_DUR    = 7,    /**< 设时长，value = 秒 */
} moxa_cmd_e;

/** ACK 的 code 字段（计划书第五节"报文规范"） */
typedef enum {
    MOXA_CODE_OK           = 0,  /**< 成功 */
    MOXA_CODE_UNKNOWN_CMD  = 1,  /**< 未知指令 */
    MOXA_CODE_BAD_PARAM    = 2,  /**< 参数非法 */
    MOXA_CODE_BUSY         = 3,  /**< 忙（队列满 / 已有配置在途） */
    MOXA_CODE_FORMAT_ERR   = 4,  /**< 格式错误（缺 msg_id、JSON 不合法） */
    MOXA_CODE_CFG_IO_ERR   = 5,  /**< 配置读写失败 */
} moxa_ack_code_e;

/** 下行指令：NetTask 解析后投给 cmd_queue */
typedef struct {
    uint16_t msg_id;     /**< 由下行指令携带，NetTask 原样透传，设备侧不分配、不回绕、不判重 */
    uint8_t  cmd;        /**< moxa_cmd_e */
    int32_t  value;      /**< 参数；query/mute/start 这类没有参数时为 0 */
    uint32_t rx_tick;    /**< 收到时刻（诊断用） */
} CmdMsg_t;

/** 回执：ControlTask 生成 → ack_queue → NetTask 发布 */
typedef struct {
    uint16_t msg_id;
    uint8_t  cmd;
    uint8_t  code;       /**< moxa_ack_code_e */
    uint8_t  state;      /**< 以 ControlTask 生成时刻为准，不在发送时刷新 */
} AckMsg_t;

/*============================ 上行待发 ============================*/

/** at_tx_queue 的元素类型：只带"待发什么"，不带大报文 */
typedef enum {
    APP_AT_TX_TELEMETRY = 1, /**< 遥测：发送时才取状态快照拼 JSON */
    APP_AT_TX_ACK       = 2, /**< 回执：发送时用下面几个字段拼 JSON */
} app_at_tx_kind_e;

typedef struct {
    uint8_t  kind;       /**< app_at_tx_kind_e */
    uint16_t msg_id;     /**< APP_AT_TX_ACK 时使用 */
    uint8_t  code;       /**< APP_AT_TX_ACK 时使用 */
    uint8_t  cmd;        /**< APP_AT_TX_ACK 时使用 */
    uint8_t  state;      /**< APP_AT_TX_ACK 时使用；取自 AckMsg_t，不在发送时刷新 */
} AtTxItem_t;

/*============================ 传感器采样 ============================*/

/** SensorTask → ControlTask。sensor_queue 的元素类型 */
typedef struct {
    uint8_t  valid;      /**< 0 = 本次采集失败，temp/humi 无意义 */
    int16_t  temp_c;
    uint16_t humi_pct;
} SensorSample_t;

/*============================ 事件位 ============================*/

/* 每个消费者一个专属位：消费者用 pdTRUE 只清自己那一位，
 * 生产者用 APP_EVT_ALL_STATE 一次广播（计划书第二节"事件位分配"） */
#define APP_EVT_STATE_ALARM   (1u << 0)   /**< 只有 AlarmTask 等这一位 */
#define APP_EVT_STATE_NET     (1u << 1)   /**< 只有 NetTask 等这一位 */
#define APP_EVT_ALL_STATE     (APP_EVT_STATE_ALARM | APP_EVT_STATE_NET)
