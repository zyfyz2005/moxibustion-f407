/**
 * @file cm_log.h
 * @brief 调试串口日志。**不依赖 HAL、不依赖 FreeRTOS**：写口和锁都由上层注册进来。
 *
 * 为什么不直接在这里引用 drv_uart / dbg_uart_mutex：
 * Common 层是"谁都能用"的地板层，如果它反过来依赖 Driver 或 Application，
 * 分层就成环了（README 第一节的原则二）。所以这里只留三个函数指针的洞，
 * 由 app_log_init() 在启动时把串口写口和互斥量填进来。
 *
 * 没注册写口之前，所有日志静默丢弃——这样纯逻辑代码在 PC 上也能链接运行。
 */

#pragma once

#include "cm_types.h"

/** 单条日志的最大长度（含结尾 '\0'），超长会被截断 */
#define CM_LOG_BUF_SIZE   224u

/** @brief 写口：把一段字节写出去，返回实际写出的字节数 */
typedef int  (*cm_log_write_fn)(const uint8_t *data, uint16_t len, void *user);

/** @brief 加锁 / 解锁回调（互斥量或关中断都行），可为 NULL */
typedef void (*cm_log_lock_fn)(void *user);

/** @brief 时间源：返回当前 uptime 秒，可为 NULL */
typedef uint32_t (*cm_log_time_fn)(void *user);

/**
 * @brief 注册写口与锁
 * @param write  写口：把一段字节写出去；传 NULL 表示丢弃所有日志
 * @param lock   加锁回调（互斥量或关中断），可为 NULL
 * @param unlock 解锁回调，与 lock 成对
 * @param user   透传给上面三个回调的用户指针
 * @note 必须在任何 cm_log_printf 之前调用；重复调用以最后一次为准
 */
void cm_log_init(cm_log_write_fn write, cm_log_lock_fn lock, cm_log_lock_fn unlock, void *user);

/**
 * @brief 注册时间源
 * @param fn   返回当前 uptime 秒；传 NULL 则日志前缀恒为 [t=0s]
 * @param user 透传指针
 * @note 日志前缀里的 [t=秒] 就来自这里
 */
void cm_log_set_time_source(cm_log_time_fn fn, void *user);

/**
 * @brief 格式化并输出一行日志
 *
 * 支持的格式符只有整数类：%d %u %x %s %c，**不支持 %f**
 * （MicroLIB 的 vsnprintf 不带浮点，写了会输出空或乱码，本工程也没有浮点字段）。
 * 自动加前缀 [t=秒] 和结尾 \r\n。
 *
 * @param fmt 格式串（printf 风格，见上面的限制）
 */
void cm_log_printf(const char *fmt, ...);

/**
 * @brief 原样输出一段字节，不走格式化缓冲
 * @note 给"看门狗即将复位前"这类场合用：此时不能再依赖静态缓冲是否可用
 */
void cm_log_write_raw(const uint8_t *data, uint16_t len);

/** @brief 是否已注册写口（没注册时上层可以少做无谓的格式化） */
bool cm_log_is_ready(void);
