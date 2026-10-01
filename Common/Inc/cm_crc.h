/**
 * @file cm_crc.h
 * @brief CRC8 与 CRC16-CCITT
 * @note 24C02 配置和 W25Q 日志都用这里的结果，全工程只此一份实现。
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief CRC-8（多项式 0x07，初值 0x00，不反射，不异或输出）
 * @note 即 CRC-8/SMBUS 参数。用于 24C02 配置块的完整性校验。
 */
#define CM_CRC8_INIT          0x00u

/**
 * @brief CRC-16/CCITT-FALSE（多项式 0x1021，初值 0xFFFF，不反射，不异或输出）
 * @note 用于 W25Q 记录块校验。
 */
#define CM_CRC16_CCITT_INIT   0xFFFFu

/** 单字节滚动，便于分段计算（例如先算头部再算载荷） */
uint8_t  cm_crc8_update(uint8_t crc, uint8_t byte);
uint16_t cm_crc16_ccitt_update(uint16_t crc, uint8_t byte);

/** 一次算完整块 */
uint8_t  cm_crc8(const void *data, size_t len);
uint16_t cm_crc16_ccitt(const void *data, size_t len);
