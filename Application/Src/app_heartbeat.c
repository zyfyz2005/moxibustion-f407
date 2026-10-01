/**
 * @file app_heartbeat.c
 * @brief 心跳实现
 */

#include "app_heartbeat.h"
#include "app_cfg.h"
#include "cm_time.h"

/* 每个任务一块，写入者是任务自己、读取者是 DogTask。
 * 32 位对齐读写在这颗核上是原子的，单个 uint32 不需要加锁。 */
static uint32_t s_last_checkin_ms[APP_HB_COUNT];

static const uint32_t s_deadline_ms[APP_HB_COUNT] = {
    [APP_HB_CTRL]   = APP_HB_CTRL_MS,
    [APP_HB_ALARM]  = APP_HB_ALARM_MS,
    [APP_HB_SENSOR] = APP_HB_SENSOR_MS,
    [APP_HB_NET]    = APP_HB_NET_MS,
    [APP_HB_CONFIG] = APP_HB_CONFIG_MS,
};

static const char *const s_task_name[APP_HB_COUNT] = {
    [APP_HB_CTRL]   = "control",
    [APP_HB_ALARM]  = "alarm",
    [APP_HB_SENSOR] = "sensor",
    [APP_HB_NET]    = "net",
    [APP_HB_CONFIG] = "config",
};

void app_hb_init(uint32_t now_ms)
{
    for (uint8_t i = 0u; i < (uint8_t)APP_HB_COUNT; i++) {
        s_last_checkin_ms[i] = now_ms;
    }
}

void app_hb_kick(app_hb_id_e id, uint32_t now_ms)
{
    if ((uint8_t)id < (uint8_t)APP_HB_COUNT) {
        s_last_checkin_ms[id] = now_ms;
    }
}

uint32_t app_hb_deadline(app_hb_id_e id)
{
    return ((uint8_t)id < (uint8_t)APP_HB_COUNT) ? s_deadline_ms[id] : 0u;
}

const char *app_hb_name(app_hb_id_e id)
{
    return ((uint8_t)id < (uint8_t)APP_HB_COUNT) ? s_task_name[id] : "?";
}

bool app_hb_check_all(uint32_t now_ms, app_hb_id_e *bad_id, uint32_t *bad_elapsed)
{
    bool all_ok = true;

    for (uint8_t i = 0u; i < (uint8_t)APP_HB_COUNT; i++) {
        uint32_t elapsed_ms = cm_elapsed_ms(now_ms, s_last_checkin_ms[i]);   /* 回绕安全 */
        if (elapsed_ms >= s_deadline_ms[i]) {
            if (all_ok) {           /* 只报告第一个超期的 */
                if (bad_id != NULL)      { *bad_id = (app_hb_id_e)i; }
                if (bad_elapsed != NULL) { *bad_elapsed = elapsed_ms; }
            }
            all_ok = false;
        }
    }
    return all_ok;
}
