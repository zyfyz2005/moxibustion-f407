/**
 * @file app_protocol.h
 * @brief MQTT 报文语义：状态/指令名映射、上行报文组装、下行指令解析。
 *
 * 传输（AT 指令、socket、重连）在 Interface/if_net.c；这里只管"报文里写什么、读什么"。
 * 报文格式见计划书第五节"报文规范"：
 *   遥测 {"type":"telemetry","device_id":...,"temp_c":...}
 *   回执 {"type":"ack","msg_id":...,"cmd":"...","result":"ok","code":0,"state":"..."}
 *   状态 {"type":"status","online":true}
 *
 * 遥测里**没有 ts 字段**：设备既没有 RTC 也没有授时通道，
 * 时间基准由 MQTT 客户端或平台的接收时刻决定（计划书明确取舍）。
 */

#pragma once

#include "app_types.h"

/*============================ 名字映射 ============================*/

/** @brief 状态名："WAITING" / "COUNTDOWN" / "ALARM"，未知值返回 "INVALID" */
const char *app_state_str(uint8_t state);

/** @brief 指令名：start / stop / reset / mute / query / set_threshold / set_duration；
 *         未知值返回 "unknown" */
const char *app_cmd_str(uint8_t cmd);

/**
 * @brief 指令名反查
 * @param name 指令名（不含引号）
 * @param len  名字长度（下行载荷不保证以 '\0' 结尾，所以长度要一起传）
 * @return moxa_cmd_e；不认识的名字返回 MOXA_CMD_UNKNOWN（0）
 */
uint8_t app_cmd_from_str(const char *name, size_t len);

/** @brief code 对应的结果串："ok" / "err" */
const char *app_code_result_str(uint8_t code);

/*============================ 上行组装 ============================*/

/**
 * @brief 组遥测报文
 * @param s   状态快照（必须已经取好快照，不要在锁里拼 JSON）
 * @param buf 输出缓冲（调用方提供，静态分配）
 * @param cap 缓冲容量，含结尾 '\0'
 * @return CM_OK；CM_ERR_FULL = 缓冲不够（病态情况，说明缓冲区设小了）
 */
cm_err_t app_proto_build_telemetry(const MoxaState_t *s, char *buf, size_t cap);

/**
 * @brief 组回执报文
 * @param a   回执内容（state 字段用的是生成时刻的状态，不要在这里刷新）
 * @param buf 输出缓冲（调用方提供，静态分配）
 * @param cap 缓冲容量，含结尾 '\0'
 * @return CM_OK；CM_ERR_FULL = 缓冲不够
 */
cm_err_t app_proto_build_ack(const AckMsg_t *a, char *buf, size_t cap);

/**
 * @brief 组在线状态报文（retained 发布；遗嘱载荷在 app_cfg.h 里单独配）
 * @param online true = {"type":"status","online":true}
 * @param buf    输出缓冲（调用方提供，静态分配）
 * @param cap    缓冲容量，含结尾 '\0'
 * @return CM_OK；CM_ERR_FULL = 缓冲不够
 */
cm_err_t app_proto_build_status(bool online, char *buf, size_t cap);

/*============================ 下行解析 ============================*/

/** 解析结果 */
typedef enum {
    APP_PARSE_OK = 0,        /**< 成功，cmd 可用 */
    APP_PARSE_ERR_FORMAT,    /**< JSON 不合法，或 cmd 字段缺失/类型不对 → code=4 */
    APP_PARSE_ERR_NO_MSGID,  /**< 缺 msg_id 或 msg_id==0 → code=4 */
} app_parse_result_e;

/** 一次解析的完整产出 */
typedef struct {
    app_parse_result_e result;
    bool               has_value;  /**< false = 报文里没有 value 字段（配置类指令会被判成 code=2） */
    CmdMsg_t           cmd;        /**< result != APP_PARSE_OK 时无意义 */
} app_parsed_cmd_t;

/**
 * @brief 解析一条下行指令
 * @param json 收到的载荷（不一定以 '\0' 结尾，所以长度必须传）
 * @param len  载荷长度，单位字节
 * @param rx_tick 收到时刻，原样放进 CmdMsg_t 供诊断
 * @note 未知指令名**不在这里报错**：映射成 MOXA_CMD_UNKNOWN 照常返回 OK，
 *       由 ControlTask 按"先 cmd 后 value"的顺序判出 code=1
 */
app_parsed_cmd_t app_proto_parse_cmd(const char *json, size_t len, uint32_t rx_tick);
