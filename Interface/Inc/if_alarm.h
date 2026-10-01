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

/** 报警类型 */
typedef enum {
    IF_ALARM_NONE = 0,      /**< 不报警 */
    IF_ALARM_TEMP,          /**< 温度报警：2 kHz */
    IF_ALARM_COUNTDOWN,     /**< 倒计时报警：3 kHz */
} if_alarm_kind_e;

/** @brief 初始化：TIM13_CH1 PWM 停振、两灯熄灭 */
cm_err_t if_alarm_init(void);

/**
 * @brief 设定当前报警类型（不改变静音标志）
 * @note 在 IF_ALARM_NONE 与某类报警之间切换时，节奏从"响"重新开始，
 *       避免切类型时出现半截节奏
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
