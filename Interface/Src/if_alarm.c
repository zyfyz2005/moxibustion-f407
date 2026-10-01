/**
 * @file if_alarm.c
 * @brief 报警输出实现：节奏推进、静音、LED 编码
 *
 * 本模块是"纯输出"：给它一个档位和当前时刻，它决定蜂鸣器和两颗灯此刻该是什么状态。
 * 它不改任何业务状态，也不参与状态机迁移——那是 ControlTask 的事。
 */

#include "if_alarm.h"

#include "drv_gpio.h"
#include "drv_pwm.h"
#include "cm_time.h"

static if_alarm_cfg_t  s_cfg;
static if_alarm_kind_e s_kind        = IF_ALARM_IDLE;
static bool            s_muted       = false;
static bool            s_sounding    = false;   /**< 蜂鸣器此刻是否在响 */
static bool            s_restart     = false;   /**< 下次 tick 时把节奏从头开始 */
static uint32_t        s_phase_start_ms = 0u;   /**< 当前节奏周期的起点 */
static bool            s_inited      = false;

/** 该档位是不是"报警档" */
static bool alarm_is_active(if_alarm_kind_e kind)
{
    return ((kind == IF_ALARM_TEMP) || (kind == IF_ALARM_COUNTDOWN));
}

/** 设置蜂鸣器输出（只在状态真的变化时才动寄存器） */
static void alarm_set_buzzer(bool on)
{
    if (on == s_sounding) {
        return;
    }
    s_sounding = on;
    if (on) {
        drv_pwm_start();
    } else {
        drv_pwm_stop();
    }
}

cm_err_t if_alarm_init(const if_alarm_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->freq_temp_hz == 0u) || (cfg->freq_countdown_hz == 0u) ||
        (cfg->on_ms == 0u) || (cfg->off_ms == 0u) || (cfg->mute_blink_ms == 0u)) {
        return CM_ERR_PARAM;
    }

    s_cfg            = *cfg;
    s_kind           = IF_ALARM_IDLE;
    s_muted          = false;
    s_sounding       = false;
    s_restart        = false;
    s_phase_start_ms = 0u;
    s_inited         = true;

    drv_pwm_stop();
    drv_gpio_led_set(DRV_GPIO_LED0, false);
    drv_gpio_led_set(DRV_GPIO_LED1, false);
    return CM_OK;
}

void if_alarm_set(if_alarm_kind_e kind)
{
    if (!s_inited || (kind == s_kind)) {
        return;
    }

    s_kind    = kind;
    s_restart = true;

    if (kind == IF_ALARM_TEMP) {
        drv_pwm_set_frequency(s_cfg.freq_temp_hz);
    } else if (kind == IF_ALARM_COUNTDOWN) {
        drv_pwm_set_frequency(s_cfg.freq_countdown_hz);
    } else {
        /* 非报警档：立刻停掉输出，不等下一次 tick */
        alarm_set_buzzer(false);
    }
}

void if_alarm_set_mute(bool muted)
{
    if (!s_inited || (muted == s_muted)) {
        return;
    }
    s_muted   = muted;
    s_restart = true;          /* 重新计时，慢闪从"亮"开始 */

    if (muted) {
        alarm_set_buzzer(false);
    }
}

void if_alarm_stop(void)
{
    s_kind    = IF_ALARM_IDLE;
    s_muted   = false;
    s_restart = false;

    alarm_set_buzzer(false);
    drv_gpio_led_set(DRV_GPIO_LED0, false);
    drv_gpio_led_set(DRV_GPIO_LED1, false);
}

if_alarm_kind_e if_alarm_get_kind(void)
{
    return s_kind;
}

void if_alarm_tick(uint32_t now_ms)
{
    uint32_t phase_ms;
    uint32_t cycle_ms;
    uint32_t pos_ms;
    bool     want_sound;

    if (!s_inited) {
        return;
    }

    if (s_restart) {
        s_restart        = false;
        s_phase_start_ms = now_ms;
    }
    phase_ms = cm_elapsed_ms(now_ms, s_phase_start_ms);

    /* LED0：只有"倒计时进行中"这一档常亮，其余全灭 */
    drv_gpio_led_set(DRV_GPIO_LED0, (s_kind == IF_ALARM_RUNNING));

    if (!alarm_is_active(s_kind)) {
        alarm_set_buzzer(false);
        drv_gpio_led_set(DRV_GPIO_LED1, false);
        return;
    }

    if (s_muted) {
        /* 静音：不响，但 LED1 慢闪——"还在报警，只是被我按静音了" */
        alarm_set_buzzer(false);
        cycle_ms  = 2u * s_cfg.mute_blink_ms;
        pos_ms    = (cycle_ms == 0u) ? 0u : (phase_ms % cycle_ms);
        drv_gpio_led_set(DRV_GPIO_LED1, (pos_ms < s_cfg.mute_blink_ms));
        return;
    }

    /* 正常报警：按"响 on_ms / 停 off_ms"循环推进 */
    cycle_ms   = s_cfg.on_ms + s_cfg.off_ms;
    pos_ms     = (cycle_ms == 0u) ? 0u : (phase_ms % cycle_ms);
    want_sound = (pos_ms < s_cfg.on_ms);

    alarm_set_buzzer(want_sound);
    drv_gpio_led_set(DRV_GPIO_LED1, want_sound);      /* LED1 与蜂鸣器同步 */
}
