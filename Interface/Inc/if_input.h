/**
 * @file if_input.h
 * @brief 按键输入服务（PA0 开关机 / PE3 静音 / PE4 复位）。
 *
 * 为什么单独有这个模块：README 的 Interface 层文件清单里只列了 sensor / net / store / alarm，
 * 但按键消抖按计划书写在 ControlTask，而"读引脚电平"属于 Driver 层，
 * 中间缺了一层。照分层规则 Application 不能直接调 Driver，所以补这个按键输入服务：
 * 消抖与边沿检测在这里做完，ControlTask 每 10 ms 拿一次"本轮新按下的键"。
 *
 * 消抖：30 ms 时间窗 + 状态机（计划书要求），全程不阻塞、不用 delay()。
 *
 * 硬件事实（已按原理图核对）：KEY 区**没有外部上拉/下拉电阻**，按键的平时电平完全靠
 * **MCU 内部上/下拉**。所以驱动初始化时必须把 PA0 配成 Pull-down（按下为高）、
 * PE3/PE4 配成 Pull-up（按下为低）；配成 No pull 会让引脚浮空，出现幽灵按键。
 */

#pragma once

#include "cm_types.h"

/** 按键位（可组合，一次可能有两键同按） */
#define IF_INPUT_KEY_POWER   (1u << 0)   /**< PA0 WK_UP，按下为高 */
#define IF_INPUT_KEY_MUTE    (1u << 1)   /**< PE3 KEY1，按下为低 */
#define IF_INPUT_KEY_RESET   (1u << 2)   /**< PE4（板子丝印 KEY0），按下为低 */
#define IF_INPUT_KEY_ALL     (IF_INPUT_KEY_POWER | IF_INPUT_KEY_MUTE | IF_INPUT_KEY_RESET)

/**
 * @brief 初始化
 * @param debounce_ms 消抖时间窗，单位 ms（计划书定的是 30）
 * @note 从参数传而不是写死：Interface 层不依赖 Application 的 `app_cfg.h`，
 *       这个值由 ControlTask 从 `APP_KEY_DEBOUNCE_MS` 取来传进来
 */
cm_err_t if_input_init(uint32_t debounce_ms);

/**
 * @brief 扫描一次，返回**本轮新产生按下沿**的按键位
 * @param now_ms 当前毫秒时基
 * @note 每 10 ms 调一次；同一按键"按住"不会重复产生事件，
 *       必须松开并重新消抖 30 ms 之后才会再产生一次按下沿
 */
uint32_t if_input_poll(uint32_t now_ms);

/**
 * @brief 查询某键当前是否处于按下状态（已消抖）
 * @note 调试用；业务逻辑请用 if_input_poll 的按下沿
 */
bool if_input_is_down(uint32_t key_mask);
