/**
 * @file if_log.h
 * @brief 日志输出服务：把 Common 层的 `cm_log` 接到调试串口上。
 *
 * 为什么单独有这么一层：`cm_log` 是 Common 层，它只能通过"注册回调"拿到写口和锁
 * （不能反过来依赖 Driver 或 Application）；而"写口是哪个串口、锁是哪个互斥量"
 * 又只有更上层知道。这个接线动作如果放在 Application 里，Application 就得直接调
 * `drv_uart`——那是跨层调用。所以放在 Interface：它本来就该知道"怎么把底下的驱动
 * 组织成一项服务"。
 *
 * 本模块不做格式化、不管日志内容，只负责：
 *   USART1（阻塞发送）+ 上层给的互斥量 → 注册进 cm_log
 */

#pragma once

#include "cm_types.h"
#include "cm_log.h"

#include "stm32f4xx_hal.h"

/**
 * @brief 初始化日志输出
 * @param huart  调试串口句柄（`huart1`）
 * @param lock   加锁回调（可为 NULL），由 Application 传进来，内部会去拿日志互斥量
 * @param unlock 解锁回调，与 lock 成对
 * @param user   透传给 lock / unlock 的指针
 * @return CM_OK；CM_ERR_PARAM huart 为空
 * @note 必须在第一次 cm_log_printf 之前调用
 */
cm_err_t if_log_init(UART_HandleTypeDef *huart,
                     cm_log_lock_fn lock, cm_log_lock_fn unlock, void *user);
