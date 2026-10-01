/**
 * @file task_config.c
 * @brief ConfigTask（优先级 1，栈 256 word）
 *
 * 职责：运行期唯一持有 I2C1 的任务——收配置请求、写 24C02（CRC8 + 按页拆包）、
 * 把结果回给 ControlTask。
 *
 * 心跳：空闲循环每 500ms 推进一次；写 24C02 期间每写完一页再由驱动回调推进一次。
 * 一次完整写只有约 15ms，2s 的期限是极宽余量，所以真正需要覆盖的是
 * "I2C 器件异常导致某页写卡住"这种情形。
 */

#include "app_tasks.h"
#include "app_ctx.h"
#include "app_cfg.h"
#include "app_heartbeat.h"

#include "if_store.h"

/** 每写完一页由 if_store_save 回调一次，用来推进心跳 */
static void config_page_done(void *user)
{
    CM_UNUSED(user);
    app_hb_kick(APP_HB_CONFIG, app_now_ms());
}

void task_config(void *arg)
{
    ConfigReq_t request;

    CM_UNUSED(arg);

    for (;;) {
        if (xQueueReceive(q_cfg_req, &request, pdMS_TO_TICKS(APP_CONFIG_IDLE_MS)) == pdTRUE) {
            ConfigResp_t response;
            bool         ok = false;

            /* config_mutex 保护 I2C1：运行期只有本任务写，加锁是让
             * "上电加载"与"运行期写"这两条路径的互斥关系在代码上看得见 */
            if (xSemaphoreTake(mtx_config, portMAX_DELAY) == pdTRUE) {
                ok = (if_store_save(&request.merged, config_page_done, NULL) == CM_OK);
                (void)xSemaphoreGive(mtx_config);
            }

            response.token = request.token;
            response.ok    = ok ? 1u : 0u;

            if (xQueueSend(q_cfg_resp, &response, 0) != pdTRUE) {
                /* 深度 2 而 ControlTask 每 10ms 必收一次，正常进不到这里 */
                app_err_inc(APP_ERR_QDROP);
            }
        }

        app_hb_kick(APP_HB_CONFIG, app_now_ms());
    }
}
