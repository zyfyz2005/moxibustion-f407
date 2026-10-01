/**
 * @file app_heartbeat.h
 * @brief 心跳：任务自己推进，DogTask 统一检查期限。
 *
 * 为什么要"任务自己推进"而不是 DogTask 代填：代填等于 DogTask 替所有任务报平安，
 * 任务真的卡死也看不出来，看门狗就白装了（计划书第六节）。
 *
 * 期限表在 app_cfg.h 里（APP_HB_*_MS），和计划书第六节一一对应。
 * DogTask 每 500ms 检查一遍，全部达标才喂 IWDG。
 */

#pragma once

#include "cm_types.h"

/** 参与心跳检查的任务（前缀与 app_cfg.h 里的 APP_HB_*_MS 保持一致） */
typedef enum {
    APP_HB_CTRL = 0,
    APP_HB_ALARM,
    APP_HB_SENSOR,
    APP_HB_NET,
    APP_HB_CONFIG,
    APP_HB_COUNT,
} app_hb_id_e;

/** @brief 用当前时刻初始化所有心跳（调度器启动前调，now_ms 一般传 0） */
void app_hb_init(uint32_t now_ms);

/**
 * @brief 任务自己推进心跳（每个任务的每个检查点调用）
 * @param id     任务 id
 * @param now_ms 当前毫秒时基（由调用方取，本模块不自己取时间）
 */
void app_hb_kick(app_hb_id_e id, uint32_t now_ms);

/** @brief 该任务的期限（ms） */
uint32_t app_hb_deadline(app_hb_id_e id);

/** @brief 该任务名（日志用） */
const char *app_hb_name(app_hb_id_e id);

/**
 * @brief 检查全部任务是否都在期限内
 * @param now_ms       当前时刻
 * @param bad_id       输出：第一个超期的任务（可为 NULL）
 * @param bad_elapsed  输出：该任务已经多久没推进（可为 NULL）
 * @return true = 全部达标（可以喂狗）
 */
bool app_hb_check_all(uint32_t now_ms, app_hb_id_e *bad_id, uint32_t *bad_elapsed);
