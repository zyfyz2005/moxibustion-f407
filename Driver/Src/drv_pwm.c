/**
 * @file drv_pwm.c
 * @brief 蜂鸣器 PWM 实现
 */

#include "drv_pwm.h"

static TIM_HandleTypeDef *s_pwm_tim   = NULL;
static bool               s_pwm_running = false;

cm_err_t drv_pwm_init(TIM_HandleTypeDef *htim)
{
    if (htim == NULL) {
        return CM_ERR_PARAM;
    }
    s_pwm_tim     = htim;
    s_pwm_running = false;
    return CM_OK;
}

void drv_pwm_set_frequency(uint32_t freq_hz)
{
    uint32_t arr;
    uint32_t ccr;

    if ((s_pwm_tim == NULL) || (freq_hz == 0u)) {
        return;
    }

    arr = (DRV_PWM_COUNTER_HZ / freq_hz);
    if (arr == 0u) {
        arr = 1u;               /* 目标频率高于计数频率时退化成最低可表达的方波 */
    }
    arr -= 1u;
    ccr = (arr + 1u) / 2u;      /* 50% 占空比：方波驱动蜂鸣器最响 */

    __HAL_TIM_SET_PRESCALER(s_pwm_tim, DRV_PWM_PRESCALER);
    __HAL_TIM_SET_AUTORELOAD(s_pwm_tim, arr);
    __HAL_TIM_SET_COMPARE(s_pwm_tim, TIM_CHANNEL_1, ccr);
}

void drv_pwm_start(void)
{
    if (s_pwm_tim == NULL) {
        return;
    }
    (void)HAL_TIM_PWM_Start(s_pwm_tim, TIM_CHANNEL_1);
    s_pwm_running = true;
}

void drv_pwm_stop(void)
{
    if (s_pwm_tim == NULL) {
        return;
    }
    /* 停通道后 CC1E 清零，OC1 输出回无效电平（本极性下是低），蜂鸣器停 */
    (void)HAL_TIM_PWM_Stop(s_pwm_tim, TIM_CHANNEL_1);
    s_pwm_running = false;
}

bool drv_pwm_is_running(void)
{
    return s_pwm_running;
}
