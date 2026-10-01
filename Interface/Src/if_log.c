/**
 * @file if_log.c
 * @brief 日志输出服务实现：只是把 cm_log 和 drv_uart 接起来
 */

#include "if_log.h"

#include "drv_uart.h"

cm_err_t if_log_init(UART_HandleTypeDef *huart,
                     cm_log_lock_fn lock, cm_log_lock_fn unlock, void *user)
{
    if (huart == NULL) {
        return CM_ERR_PARAM;
    }
    if (drv_uart_debug_init(huart) != CM_OK) {
        return CM_ERR_IO;
    }

    /* 写口固定是调试串口的阻塞写；锁和时间源由上层提供 */
    cm_log_init(drv_uart_debug_write, lock, unlock, user);
    return CM_OK;
}
