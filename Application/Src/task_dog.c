/**
 * @file task_dog.c
 * @brief DogTask（优先级 7，栈 256 word）
 *
 * 职责：每 500ms 检查其余 5 个任务的心跳是否都在各自期限内，
 * **全部达标才喂 IWDG**；任一超期就停止喂狗，让 IWDG 把系统复位。
 *
 * 最坏复位时间 = 最大心跳期限(2.5s) + 本任务检查周期(0.5s) + IWDG 超时(4s) = 7.0s，
 * 验收标准按 ≤8s 执行（计划书第六节）。
 *
 * 本任务自己不参与心跳检查：它要是卡死，狗自然没人喂，一样会复位；
 * 再给自己记一次心跳反而是"自己给自己报平安"，没有意义。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_heartbeat.h"

#include "cm_log.h"

void task_dog(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    app_hb_id_e last_stuck_id = APP_HB_COUNT;   /* 上一次报出来的超期任务 */
    uint16_t    repeat_count  = 0u;             /* 同一任务连续超期的次数 */

    CM_UNUSED(arg);

    for (;;) {
        app_hb_id_e stuck_id   = APP_HB_COUNT;
        uint32_t    elapsed_ms = 0u;
        uint32_t    now;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_DOG_PERIOD_MS));
        now = app_now_ms();

        if (app_hb_check_all(now, &stuck_id, &elapsed_ms)) {
            if (last_stuck_id != APP_HB_COUNT) {
                cm_log_printf("hb: recovered (%s)", app_hb_name(last_stuck_id));
            }
            last_stuck_id = APP_HB_COUNT;
            repeat_count  = 0u;
            app_wdt_feed();
        } else {
            if (stuck_id != last_stuck_id) {
                cm_log_printf("hb: TIMEOUT %s (%ums) -> stop feeding dog",
                              app_hb_name(stuck_id), (unsigned)elapsed_ms);
                last_stuck_id = stuck_id;
                repeat_count  = 0u;
            } else {
                repeat_count++;
                /* 每 10s 提醒一次，别把串口刷满 */
                if ((repeat_count % 20u) == 0u) {
                    cm_log_printf("hb: still stuck %s (%ums)",
                                  app_hb_name(stuck_id), (unsigned)elapsed_ms);
                }
            }
            /* 不喂狗：APP_IWDG_ENABLE=1 时 4s 后复位；
             * =0（M1~M4）时只留日志，方便边调边看 */
        }
    }
}
