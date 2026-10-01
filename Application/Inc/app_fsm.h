/**
 * @file app_fsm.h
 * @brief 状态机（WAITING / COUNTDOWN / ALARM + 正交的 muted 标志）。
 *
 * 设计成"只操作传进来的 MoxaState_t、不碰任何全局量"的纯函数集合：
 *   - ControlTask 持有一份本地状态，改完再 app_state_commit() 提交；
 *   - 可以在 PC 上单独跑逻辑测试，不需要硬件和 FreeRTOS。
 *
 * 与计划书第四节的两处出入（已在实现里落地，需要你确认）：
 *   1. **开关机键在运行中按下**：计划书的迁移表只写了"复位键 / MQTT stop 回 WAITING"，
 *      没有写开关机键。但这个键的名字就是"开关机"，按下去没反应不合理，
 *      所以实现成：WAITING 下按下 → 开机；COUNTDOWN/ALARM 下按下 → 关机回 WAITING。
 *   2. **倒计时在 ALARM 期间继续走**：如果温度报警期间倒计时停住，
 *      "ALARM → COUNTDOWN 需要 remain_s > 0" 这个条件就永远成立，报警永远不会
 *      转成"归零保持型"。所以实现成：只要 switch_on=1，倒计时在 COUNTDOWN 和 ALARM
 *      两个状态下都在递减。
 */

#pragma once

#include "app_types.h"

/**
 * @brief 用配置初始化状态（上电时调一次）
 * @param s   待初始化的状态对象
 * @param cfg 从 24C02 读出的配置（阈值与倒计时时长）
 */
void app_fsm_init(MoxaState_t *s, const moxa_cfg_t *cfg);

/** @brief 开机：WAITING → COUNTDOWN，remain_s 重装为 duration_s */
void app_fsm_start(MoxaState_t *s);

/** @brief 关机：任何状态 → WAITING，muted 清零 */
void app_fsm_stop(MoxaState_t *s);

/** @brief 复位：任何状态 → WAITING，muted 清零，倒计时重装 */
void app_fsm_reset(MoxaState_t *s);

/** @brief 开关机键：WAITING 下开机，其余状态下关机 */
void app_fsm_power_key(MoxaState_t *s);

/** @brief 静音键：只在 ALARM 期间有效，把 muted 置 1 */
void app_fsm_mute_key(MoxaState_t *s);

/** @brief 每秒调一次：递减倒计时（COUNTDOWN 与 ALARM 期间都走） */
void app_fsm_tick_1s(MoxaState_t *s);

/**
 * @brief 评估报警条件并做状态迁移。每轮轮询都调（10ms 一次）
 * @note 会刷新 temp_alarm / countdown_alarm，并按"两个条件是否成立"迁移状态
 */
void app_fsm_eval(MoxaState_t *s);

/**
 * @brief 更新温度与有效性（SensorTask 的采样经 ControlTask 落到这里）
 * @param s        状态对象
 * @param valid    false = 本周期读取失败；此时保留上次温度值但把 temp_valid 清 0
 * @param temp_c   温度，整数 ℃，仅 valid = true 时有意义
 * @param humi_pct 湿度，整数 %，仅 valid = true 时有意义
 */
void app_fsm_set_temp(MoxaState_t *s, bool valid, int16_t temp_c, uint16_t humi_pct);

/** @brief 更新阈值（配置写成功后由 ControlTask 调） */
void app_fsm_set_threshold(MoxaState_t *s, uint16_t th_max_c);

/** @brief 更新倒计时时长（配置写成功后由 ControlTask 调） */
void app_fsm_set_duration(MoxaState_t *s, uint32_t duration_s);
