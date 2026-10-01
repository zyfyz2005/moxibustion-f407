/**
 * @file task_sensor.c
 * @brief SensorTask（优先级 3，栈 256 word）
 *
 * 职责：每 2s 读一次 DHT11，把结果投给 ControlTask。
 * 心跳在采集前后各推进一次——一次采集要阻塞约 4ms，中间没有别的检查点，
 * 所以计划书给 SensorTask 的期限是 2.5s（比采集周期 2s 宽 0.5s）。
 *
 * 读失败不重试当前周期：记一笔统计，等下一个 2s 周期重读（计划书第四节）。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_heartbeat.h"

#include "if_sensor.h"

void task_sensor(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    CM_UNUSED(arg);

    (void)if_sensor_init();

    for (;;) {
        SensorSample_t sample;

        app_hb_kick(APP_HB_SENSOR, app_now_ms());     /* 采集开始前 */

        sample.valid    = 0u;
        sample.temp_c   = 0;
        sample.humi_pct = 0u;
        if (if_sensor_read(&sample.temp_c, &sample.humi_pct) == CM_OK) {
            sample.valid = 1u;
        }

        app_hb_kick(APP_HB_SENSOR, app_now_ms());     /* 采集结束后（覆盖 4ms 阻塞） */

        if (xQueueSend(q_sensor, &sample, 0) != pdTRUE) {
            /* 满：丢最旧再入队。sensor_queue 的队头就是最旧的一条。
             * 这里不单独计错误：生产周期 2s、消费周期 10ms，
             * 正常时序下这个分支结构上不可能进，真进了说明系统另有问题 */
            SensorSample_t dropped;
            (void)xQueueReceive(q_sensor, &dropped, 0);
            (void)xQueueSend(q_sensor, &sample, 0);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));
    }
}
