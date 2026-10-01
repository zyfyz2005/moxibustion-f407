/**
 * @file drv_24c02.h
 * @brief 24C02（2 Kbit I2C EEPROM）字节读写与跨页拆包。
 *
 * 硬件事实：接在 PB8=SCL / PB9=SDA（I2C1，AF4），A0/A1/A2 全接地，
 * WP 悬空（可写）。**7 位从机地址 0x50**；HAL 的接口要的是 8 位地址，
 * 所以内部左移一位用 **0xA0**。
 *
 * 页写约束：AT24C02 页大小 **8 字节**，一次写不能跨页——
 * 跨页写会回卷覆盖本页开头的数据。本驱动按页边界自动拆包，
 * 每页写完等一个内部写周期（`DRV_24C02_WRITE_CYCLE_MS`）。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/** 7 位从机地址（原理图：A0/A1/A2 接地 → 0x50） */
#define DRV_24C02_ADDR_7BIT         0x50u
/** 8 位地址（HAL 的 DevAddress 参数要这个值） */
#define DRV_24C02_ADDR_8BIT         (DRV_24C02_ADDR_7BIT << 1)
/** 页大小，单位字节 */
#define DRV_24C02_PAGE_SIZE         8u
/** 一次页写后的内部写周期，单位 ms（数据手册最大 5 ms） */
#define DRV_24C02_WRITE_CYCLE_MS    5u
/** HAL 调用的超时，单位 ms */
#define DRV_24C02_TIMEOUT_MS        50u

/** 每写完一页回调一次（用于在长写过程中推进心跳） */
typedef void (*drv_24c02_page_fn)(void *user);

/**
 * @brief 绑定 I2C 句柄
 * @param hi2c CubeMX 生成的 I2C1 句柄（`hi2c1`）
 */
cm_err_t drv_24c02_init(I2C_HandleTypeDef *hi2c);

/**
 * @brief 探测器件是否应答
 * @return true = 器件在总线上应答（M1 就是靠它验证 I2C 通了）
 */
bool drv_24c02_is_ready(void);

/**
 * @brief 从任意地址读一段
 * @param mem_addr 器件内地址，0..255
 * @param dst      输出缓冲
 * @param len      读取字节数
 * @return CM_OK；CM_ERR_PARAM / CM_ERR_IO（器件没应答或超时）
 */
cm_err_t drv_24c02_read(uint8_t mem_addr, uint8_t *dst, uint16_t len);

/**
 * @brief 从任意地址写一段（内部按 8 字节页边界拆包）
 * @param mem_addr      起始地址，0..255
 * @param src           源数据
 * @param len           写入字节数
 * @param on_page_done  每写完一页回调一次，可为 NULL
 * @param user          透传指针
 * @return CM_OK；CM_ERR_PARAM / CM_ERR_IO
 * @note 阻塞：每页之间要等 5 ms 写周期，写满 256 字节约 160 ms
 */
cm_err_t drv_24c02_write(uint8_t mem_addr, const uint8_t *src, uint16_t len,
                         drv_24c02_page_fn on_page_done, void *user);
