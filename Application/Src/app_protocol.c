/**
 * @file app_protocol.c
 * @brief 报文语义实现
 */

#include "app_protocol.h"
#include "app_cfg.h"

#include "cm_json.h"

#include <string.h>

/*============================ 名字映射 ============================*/

const char *app_state_str(uint8_t state)
{
    switch ((moxa_state_e)state) {
    case MOXA_ST_WAITING:   return "WAITING";
    case MOXA_ST_COUNTDOWN: return "COUNTDOWN";
    case MOXA_ST_ALARM:     return "ALARM";
    default:                return "INVALID";
    }
}

const char *app_cmd_str(uint8_t cmd)
{
    switch ((moxa_cmd_e)cmd) {
    case MOXA_CMD_START:    return "start";
    case MOXA_CMD_STOP:     return "stop";
    case MOXA_CMD_RESET:    return "reset";
    case MOXA_CMD_MUTE:     return "mute";
    case MOXA_CMD_QUERY:    return "query";
    case MOXA_CMD_SET_TH:   return "set_threshold";
    case MOXA_CMD_SET_DUR:  return "set_duration";
    default:                return "unknown";
    }
}

/** 指令名表：加一条新指令只改这里和 moxa_cmd_e */
typedef struct {
    const char *name;
    uint8_t     cmd;
} cmd_name_map_t;

static const cmd_name_map_t s_cmd_map[] = {
    { "start",         (uint8_t)MOXA_CMD_START   },
    { "stop",          (uint8_t)MOXA_CMD_STOP    },
    { "reset",         (uint8_t)MOXA_CMD_RESET   },
    { "mute",          (uint8_t)MOXA_CMD_MUTE    },
    { "query",         (uint8_t)MOXA_CMD_QUERY   },
    { "set_threshold", (uint8_t)MOXA_CMD_SET_TH  },
    { "set_duration",  (uint8_t)MOXA_CMD_SET_DUR },
};

uint8_t app_cmd_from_str(const char *name, size_t len)
{
    if ((name == NULL) || (len == 0u)) {
        return (uint8_t)MOXA_CMD_UNKNOWN;
    }

    for (size_t i = 0u; i < CM_ARRAY_LEN(s_cmd_map); i++) {
        const char *candidate = s_cmd_map[i].name;
        size_t      name_len    = 0u;
        bool        same = true;

        while (candidate[name_len] != '\0') {
            name_len++;
        }
        if (name_len != len) {
            continue;
        }
        for (size_t j = 0u; j < len; j++) {
            if (name[j] != candidate[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return s_cmd_map[i].cmd;
        }
    }
    return (uint8_t)MOXA_CMD_UNKNOWN;
}

const char *app_code_result_str(uint8_t code)
{
    return (code == (uint8_t)MOXA_CODE_OK) ? "ok" : "err";
}

/*============================ 上行组装 ============================*/

cm_err_t app_proto_build_telemetry(const MoxaState_t *s, char *buf, size_t cap)
{
    cm_json_w_t writer;

    if ((s == NULL) || (buf == NULL) || (cap == 0u)) {
        return CM_ERR_PARAM;
    }

    cm_json_w_init(&writer, buf, cap);
    (void)cm_json_w_begin_obj(&writer);
    (void)cm_json_w_kv_str(&writer, "type", "telemetry");
    (void)cm_json_w_kv_u32(&writer, "device_id", (uint32_t)DEVICE_ID);
    (void)cm_json_w_kv_int(&writer, "temp_c", (int32_t)s->temp_c);
    (void)cm_json_w_kv_u32(&writer, "humi_pct", (uint32_t)s->humi_pct);
    (void)cm_json_w_kv_u32(&writer, "remain_s", s->remain_s);
    (void)cm_json_w_kv_str(&writer, "state", app_state_str(s->state));
    (void)cm_json_w_kv_int(&writer, "temp_alarm", (int32_t)s->temp_alarm);
    (void)cm_json_w_kv_int(&writer, "countdown_alarm", (int32_t)s->countdown_alarm);
    (void)cm_json_w_kv_int(&writer, "muted", (int32_t)s->muted);
    (void)cm_json_w_kv_u32(&writer, "th_max_c", (uint32_t)s->th_max_c);
    (void)cm_json_w_kv_u32(&writer, "duration_s", s->duration_s);
    (void)cm_json_w_kv_u32(&writer, "err_cnt", (uint32_t)s->err_cnt);
    (void)cm_json_w_kv_u32(&writer, "uptime_s", s->uptime_s);
    (void)cm_json_w_kv_str(&writer, "fw", APP_FW_VERSION);
    (void)cm_json_w_end_obj(&writer);

    return cm_json_w_ok(&writer) ? CM_OK : CM_ERR_FULL;
}

cm_err_t app_proto_build_ack(const AckMsg_t *a, char *buf, size_t cap)
{
    cm_json_w_t writer;

    if ((a == NULL) || (buf == NULL) || (cap == 0u)) {
        return CM_ERR_PARAM;
    }

    cm_json_w_init(&writer, buf, cap);
    (void)cm_json_w_begin_obj(&writer);
    (void)cm_json_w_kv_str(&writer, "type", "ack");
    (void)cm_json_w_kv_u32(&writer, "msg_id", (uint32_t)a->msg_id);
    (void)cm_json_w_kv_str(&writer, "cmd", app_cmd_str(a->cmd));
    (void)cm_json_w_kv_str(&writer, "result", app_code_result_str(a->code));
    (void)cm_json_w_kv_int(&writer, "code", (int32_t)a->code);
    (void)cm_json_w_kv_str(&writer, "state", app_state_str(a->state));
    (void)cm_json_w_end_obj(&writer);

    return cm_json_w_ok(&writer) ? CM_OK : CM_ERR_FULL;
}

cm_err_t app_proto_build_status(bool online, char *buf, size_t cap)
{
    cm_json_w_t writer;

    if ((buf == NULL) || (cap == 0u)) {
        return CM_ERR_PARAM;
    }

    cm_json_w_init(&writer, buf, cap);
    (void)cm_json_w_begin_obj(&writer);
    (void)cm_json_w_kv_str(&writer, "type", "status");
    (void)cm_json_w_kv_str(&writer, "online", online ? "true" : "false");
    (void)cm_json_w_end_obj(&writer);

    return cm_json_w_ok(&writer) ? CM_OK : CM_ERR_FULL;
}

/*============================ 下行解析 ============================*/

app_parsed_cmd_t app_proto_parse_cmd(const char *json, size_t len, uint32_t rx_tick)
{
    app_parsed_cmd_t  parsed;
    int32_t           msg_id = 0;
    int32_t           value  = 0;
    char              cmd_name[24];

    parsed.result    = APP_PARSE_ERR_FORMAT;
    parsed.has_value = false;
    parsed.cmd.msg_id   = 0u;
    parsed.cmd.cmd      = (uint8_t)MOXA_CMD_UNKNOWN;
    parsed.cmd.value    = 0;
    parsed.cmd.rx_tick  = rx_tick;

    if ((json == NULL) || (len == 0u)) {
        return parsed;
    }

    /* 先判 msg_id：缺了它这条指令无法回执，客户端也无从对上号 */
    if (!cm_json_get_i32(json, len, "msg_id", &msg_id)) {
        parsed.result = APP_PARSE_ERR_NO_MSGID;
        return parsed;
    }
    if ((msg_id <= 0) || (msg_id > 65535)) {
        /* 0 按"未提供"处理；超出 16 位的值设备侧装不下，也按格式错算 */
        parsed.result = APP_PARSE_ERR_NO_MSGID;
        return parsed;
    }

    if (!cm_json_get_str(json, len, "cmd", cmd_name, sizeof(cmd_name))) {
        parsed.result = APP_PARSE_ERR_FORMAT;
        return parsed;
    }

    parsed.cmd.msg_id = (uint16_t)msg_id;
    parsed.cmd.cmd    = app_cmd_from_str(cmd_name, strlen(cmd_name));

    /* value 是可选字段：只有配置类指令需要它。
     * 这里不去判"该不该有值"，那是 ControlTask 的判定顺序（先 cmd 后 value） */
    if (cm_json_get_i32(json, len, "value", &value)) {
        parsed.cmd.value = value;
        parsed.has_value = true;
    }

    parsed.result = APP_PARSE_OK;
    return parsed;
}
