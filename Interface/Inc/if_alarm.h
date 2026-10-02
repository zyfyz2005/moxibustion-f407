/**
 * @file if_alarm.h
 * @brief 报警输出服务（蜂鸣器 PWM PF8 + 状态灯 PF9/PF10）。
 *
 * 本模块负责"报警长什么样"：蜂鸣器频率、响/停节奏、LED 表现。
 * AlarmTask 只负责"什么时候该响"——等事件位、取状态、交给这里。
 * 这样把节奏这种确定性的东西收进一个模块，任务里就不会散落一堆计时魔数。
 *
 * 节奏与频率（计划书第三节 M0 结论）：
 *   温度报警   2 kHz   响 200 ms / 停 150 ms   → TIM13 ARR 499 / CCR 250
 *   倒计时报警 3 kHz   响 200 ms / 停 150 ms   → TIM13 ARR 332 / CCR 166
 *   温度与倒计时同时成立时用 3 kHz。理由：倒计时归零是保持型报警，
 *   必须复位才能退出，优先级高于可自愈的温度报警。此优先级是本次实现定的，计划书未明确。
 *
 * 定时器：**PF8 只能走 TIM13_CH1**（TIM11_CH1 在 PF7，而 PF7 板上接光敏）。
 * TIM13 在 APB1 → 定时器时钟 84 MHz，PSC 取 83 得到 1 MHz 计数（不是 TIM11 在 APB2 时的 167）。
 * 已按 CubeMX 芯片库与 CMSIS 头文件复核，见计划书第三节"原理图复核结果"。
 *
 * LED 编码（计划书只说"用 LED 编码表示状态"，没给具体编码，这里定一套）：
 *   WAITING      两灯全灭
 *   COUNTDOWN    LED0 常亮、LED1 灭
 *   ALARM        LED1 跟着蜂鸣器亮灭（响则亮）
 *   ALARM+muted  LED1 慢闪 500 ms 亮 / 500 ms 灭（表示"仍在报警但已静音"）
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/**
 * 报警输出的"档位"。
 * 这里刻意**不叫** WAITING/COUNTDOWN/ALARM——那是 ControlTask 的状态机词汇，
 * 本模块只管"输出长什么样"。AlarmTask 负责把状态机的状态映射到这几个档位上。
 */
typedef enum {
    IF_ALARM_IDLE = 0,      /**< 待机：两灯全灭、蜂鸣器不响 */
    IF_ALARM_RUNNING,       /**< 倒计时进行中：LED0 常亮、蜂鸣器不响 */
    IF_ALARM_TEMP,          /**< 温度报警：2 kHz，LED1 跟着响 */
    IF_ALARM_COUNTDOWN,     /**< 倒计时归零报警：3 kHz，LED1 跟着响 */
} if_alarm_kind_e;

/** 输出参数（由 AlarmTask 从 app_cfg.h 取好传进来，避免 Interface 依赖 Application） */
typedef struct {
    uint32_t freq_temp_hz;      /**< 温度报警频率，单位 Hz（计划书：2000） */
    uint32_t freq_countdown_hz; /**< 倒计时报警频率，单位 Hz（计划书：3000） */
    uint32_t on_ms;             /**< 报警节奏的"响"段，单位 ms（计划书：200） */
    uint32_t off_ms;            /**< 报警节奏的"停"段，单位 ms（计划书：150） */
    uint32_t mute_blink_ms;     /**< 静音时 LED1 慢闪的半周期，单位 ms */
} if_alarm_cfg_t;

/**
 * @brief 初始化：把 PWM 句柄交给驱动、停振、两灯熄灭
 * @param cfg  输出参数（频率与节奏）
 * @param htim CubeMX 生成的 PWM 句柄（`htim13`，对应 PF8 / TIM13_CH1）
 * @return CM_OK；CM_ERR_PARAM 入参为空、或句柄绑定失败
 * @note **必须在任何 if_alarm_set / if_alarm_tick 之前调用**：本函数会把句柄转交给
 *       `drv_pwm`。漏掉这一步的后果很隐蔽——编译链接都正常，只是蜂鸣器永远不响
 *       （驱动内部句柄为 NULL，set_frequency / start 会直接返回）。
 *       2026-10-02 上板时就踩了这个：现象是"除了不响都正常"。
 */
cm_err_t if_alarm_init(const if_alarm_cfg_t *cfg, TIM_HandleTypeDef *htim);

/**
 * @brief 切换输出档位（不改变静音标志）
 * @param kind 目标档位
 * @note 档位变化时节奏从"响"重新开始，避免切档时出现半截节奏；
 *       从报警档切到 IF_ALARM_IDLE / IF_ALARM_RUNNING 会停掉蜂鸣器
 */
void if_alarm_set(if_alarm_kind_e kind);

/**
 * @brief 设置静音
 * @param muted true = 停 PWM（LED1 改慢闪），但内部仍保持"正在报警"
 */
void if_alarm_set_mute(bool muted);

/** @brief 退出报警：停 PWM、灭灯 */
void if_alarm_stop(void);

/** @brief 当前报警类型 */
if_alarm_kind_e if_alarm_get_kind(void);

/**
 * @brief 节奏推进
 * @param now_ms 当前毫秒时基（来自调用方，模块内部不取时间）
 * @note AlarmTask 每 50 ms 调一次；本函数按传入时间推进，不依赖调用次数
 */
void if_alarm_tick(uint32_t now_ms);
