/**
 * @file drv_pwm.h
 * @brief 蜂鸣器 PWM（PF8 / TIM13_CH1）。
 *
 * 硬件事实：蜂鸣器是**无源**的，靠 Q2（S8050 NPN）低端驱动、**高电平导通**，
 * 所以"响"就是让 PF8 输出方波，"不响"就是停掉 PWM（输出回到低电平）。
 *
 * 时基换算（已按 CMSIS 头文件复核 TIM13 挂在 APB1）：
 *   TIM13 时钟 = 84 MHz（APB1 42 MHz × 2），PSC 固定 83 → 计数频率 1 MHz；
 *   ARR = 1 MHz / 目标频率 - 1，占空比固定 50%（方波最响）。
 *   例：2 kHz → ARR 499 / CCR 250；3 kHz → ARR 332 / CCR 166。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/** 计数频率（Hz）：TIM13 时钟 84 MHz ÷ (PSC+1)，PSC = 83 */
#define DRV_PWM_COUNTER_HZ      1000000u
/** 预分频值（寄存器值，实际分频 = 83+1 = 84） */
#define DRV_PWM_PRESCALER       83u

/**
 * @brief 绑定 TIM13 句柄
 * @param htim CubeMX 生成的 PWM 句柄（`htim13`）
 * @note 只记录句柄，不启动输出；要响还得调 drv_pwm_start()
 */
cm_err_t drv_pwm_init(TIM_HandleTypeDef *htim);

/**
 * @brief 设定输出频率（内部按 1 MHz 计数重算 ARR 与 CCR）
 * @param freq_hz 目标频率，单位 Hz；传 0 视为非法、不改变当前设置
 * @note 可以在运行中调用：只改 ARR/CCR 寄存器，不重建定时器
 */
void drv_pwm_set_frequency(uint32_t freq_hz);

/** @brief 开始输出方波（蜂鸣器响） */
void drv_pwm_start(void);

/** @brief 停止输出（蜂鸣器停；引脚回低电平） */
void drv_pwm_stop(void);

/** @brief 当前是否正在输出 */
bool drv_pwm_is_running(void);
