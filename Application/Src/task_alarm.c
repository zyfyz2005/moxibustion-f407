/**
 * @file task_alarm.c
 * @brief AlarmTask（优先级 4，栈 256 word）
 *
 * 职责：等状态事件位 → 取状态快照 → 告诉 if_alarm 该报警还是该停 → 推进节奏。
 *
 * 为什么周期是 50ms：报警节奏是"响 200ms / 停 150ms"，用 50ms 的粒度去切，
 * 每个沿的误差最多 50ms，听感上没有区别；同时天然满足
 * "AlarmTask 每 50ms 推进一次心跳、期限 500ms"的要求。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_heartbeat.h"

#include "if_alarm.h"

void task_alarm(void *arg)
{
    MoxaState_t state;

    CM_UNUSED(arg);

    (void)if_alarm_init();

    for (;;) {
        uint32_t now;

        /* 等状态变更事件；最多等一个节奏周期就醒，保证节奏按时推进。
         * pdTRUE = 只清自己这一位（APP_EVT_STATE_ALARM） */
        (void)xEventGroupWaitBits(eg_state, APP_EVT_STATE_ALARM, pdTRUE, pdFALSE,
                                  pdMS_TO_TICKS(APP_ALARM_TICK_MS));

        now = app_now_ms();
        app_state_take(&state);

        if (state.state == (uint8_t)MOXA_ST_ALARM) {
            /* 两类报警同时成立时用倒计时频率：它是保持型，必须复位才能退出 */
            if (state.countdown_alarm != 0u) {
                if_alarm_set(IF_ALARM_COUNTDOWN);
            } else if (state.temp_alarm != 0u) {
                if_alarm_set(IF_ALARM_TEMP);
            } else {
                if_alarm_set(IF_ALARM_NONE);
            }
            if_alarm_set_mute(state.muted != 0u);
        } else {
            if_alarm_stop();
        }

        if_alarm_tick(now);
        app_hb_kick(APP_HB_ALARM, now);
    }
}
