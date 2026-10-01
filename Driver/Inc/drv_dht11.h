/**
 * @file drv_dht11.h
 * @brief DHT11 温湿度传感器（PG9 单总线）。
 *
 * 硬件事实：数据脚 **PG9**，板载 R36 10K 上拉到 VCC3.3。
 * CubeMX 把 PG9 配成 **开漏输出（OD）+ 初始高**，本驱动就用这个特性工作：
 *   写 0 = 把线拉低；写 1 = 释放（外部上拉拉高）；读 IDR 任何时候都能拿到线上的实际电平。
 * 所以**不需要来回切 MODER**，"切输入"这件事由开漏 + 外部上拉天然完成。
 *
 * 位级延时用 **DWT 的 CYCCNT**（Cortex-M4 自带），不用 HAL_Delay：
 * HAL_Delay 的最小单位是 1 ms，而 DHT11 的位时隙只有 26~70 µs。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/** 数据脚（PG9，开漏 + 板载 10K 上拉） */
#define DRV_DHT11_PORT              GPIOG
#define DRV_DHT11_PIN               GPIO_PIN_9

/** 起始信号拉低时长，单位 ms */
#define DRV_DHT11_START_LOW_MS      18u
/** 两次采集之间至少要隔这么久，单位 ms（DHT11 数据手册要求 ≥1 s） */
#define DRV_DHT11_MIN_INTERVAL_MS   1000u

/**
 * @brief 初始化：使能 DWT 计数器并把数据线释放为高
 * @return 永远是 CM_OK（没有可失败的步骤）
 */
cm_err_t drv_dht11_init(void);

/**
 * @brief 读一次温湿度
 * @param temp_c   温度，整数 ℃（DHT11 分辨率就是 1 ℃）
 * @param humi_pct 湿度，整数 %
 * @return CM_OK 成功且校验和正确；CM_ERR_TIMEOUT 器件没响应；CM_ERR_CRC 校验和错
 * @note 阻塞约 20~25 ms（18 ms 起始低电平 + 40 bit 收发），
 *       调用方必须在调用前后各推进一次心跳；两次调用之间要间隔 ≥1 s
 */
cm_err_t drv_dht11_read(int16_t *temp_c, uint16_t *humi_pct);
