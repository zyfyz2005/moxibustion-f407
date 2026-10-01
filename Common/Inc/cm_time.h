/**
 * @file cm_time.h
 * @brief 时间换算与小工具。**不依赖 HAL、不依赖 FreeRTOS**：tick 值一律由调用方传进来。
 *
 * 为什么不在这里包一层"取当前 tick"：
 * 那样 Common 就得知道 HAL 或 FreeRTOS 的存在。时间源由上层提供，
 * 这个文件只负责"拿两个时间点算差值"这类纯计算，因此可以在 PC 上单测。
 */

#pragma once

#include "cm_types.h"

/** 一天 = 86400 秒，肉眼换算日志里的 uptime 用 */
#define CM_SEC_PER_DAY   86400u

/**
 * @brief 回绕安全的差值计算
 * @note 直接写 now - earlier 在 32 位无符号数上是标准做法，49.7 天回绕时结果依然正确；
 *       前提是两次采样间隔不超过 2^32 ms（约 49.7 天），本项目最小采样间隔是 10ms。
 */
static inline uint32_t cm_elapsed_ms(uint32_t now_ms, uint32_t earlier_ms)
{
    return (uint32_t)(now_ms - earlier_ms);
}

/**
 * @brief 判断从 start_ms 起算是否已超时
 * @param timeout_ms 传 0 表示"立刻超时"
 */
static inline bool cm_timeout_expired(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms)
{
    return (cm_elapsed_ms(now_ms, start_ms) >= timeout_ms);
}

/**
 * @brief 剩余超时时间（已超时返回 0）
 * @note xEventGroupWaitBits / xQueueReceive 的阻塞时间用得上：把"到期时刻"换成"还剩多久"
 */
static inline uint32_t cm_timeout_left(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms)
{
    uint32_t used = cm_elapsed_ms(now_ms, start_ms);
    return (used >= timeout_ms) ? 0u : (timeout_ms - used);
}

/**
 * @brief 独立秒计数器
 * @note 专门用来算 uptime：不用 HAL_GetTick()/1000，因为那是 32 位毫秒，
 *       约 49.7 天回绕，除 1000 之后在第 49.7 天会跳变。这里自己累加，
 *       可跑约 136 年（2^32 秒）。
 */
typedef struct {
    uint32_t acc_ms;   /**< 不够 1000ms 的余量 */
    uint32_t sec;      /**< 累计秒数 */
} cm_sec_counter_t;

/** @brief 复位秒计数器 */
static inline void cm_sec_counter_reset(cm_sec_counter_t *c)
{
    c->acc_ms = 0u;
    c->sec    = 0u;
}

/**
 * @brief 累加一段毫秒，满 1000ms 就进一秒
 * @param delta_ms 本次推进的毫秒数（应等于调用周期，例如 10）
 * @return 本次是否产生了新的整秒
 */
bool cm_sec_counter_add(cm_sec_counter_t *c, uint32_t delta_ms);
