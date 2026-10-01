/**
 * @file if_sensor.h
 * @brief 温湿度采集服务（DHT11，PG9 单总线）。
 *
 * 业务层只认这个接口：读一次温湿度、拿采集统计。
 * 统计是给验收用的——计划书要求"连续 500 个采集周期，总失败率 ≤1%、最大连续失败 ≤2"，
 * 所以这里顺手把总周期数、失败数、当前连续失败、最大连续失败都记下来，
 * 串口一条日志就能看出是否达标，不用人工数。
 */

#pragma once

#include "cm_types.h"

/** 采集统计 */
typedef struct {
    uint32_t total;        /**< 累计采集周期数（含失败） */
    uint32_t fail;         /**< 累计失败次数 */
    uint16_t consec;       /**< 当前连续失败次数 */
    uint16_t max_consec;   /**< 最大连续失败次数（只增不减，用于对照验收阈值） */
} if_sensor_stat_t;

/** @brief 初始化（把 PG9 配成开漏输出 + 上拉，准备分时切方向） */
cm_err_t if_sensor_init(void);

/**
 * @brief 读一次温湿度
 * @param temp_c   温度，整数 ℃（DHT11 本身分辨率就是 1 ℃，不做小数）
 * @param humi_pct 相对湿度，整数 %
 * @note 阻塞约 4 ms（40 bit × 位时隙），调用方必须在调用前后各自推进一次心跳
 * @return CM_OK 成功且校验和正确；CM_ERR_TIMEOUT 器件没响应；CM_ERR_CRC 校验和错
 */
cm_err_t if_sensor_read(int16_t *temp_c, uint16_t *humi_pct);

/** @brief 取统计（只读） */
const if_sensor_stat_t *if_sensor_get_stat(void);

/** @brief 清零统计（每轮 500 周期验收前调一次） */
void if_sensor_reset_stat(void);
