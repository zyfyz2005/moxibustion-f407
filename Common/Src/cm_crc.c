/**
 * @file cm_crc.c
 * @brief CRC8 与 CRC16-CCITT 实现（逐位移位，不用查表）
 * @note 逐位实现比查表慢，但这里的数据量是"每几秒几十字节"，性能毫无压力，
 *       换来的是不带 512 字节常量表、代码可读、方便对着算法核对。
 */

#include "cm_crc.h"

uint8_t cm_crc8_update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0; i < 8u; i++) {
        if (crc & 0x80u) {
            crc = (uint8_t)((crc << 1) ^ 0x07u);
        } else {
            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t cm_crc16_ccitt_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)((uint16_t)byte << 8);
    for (uint8_t i = 0; i < 8u; i++) {
        if (crc & 0x8000u) {
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t cm_crc8(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = CM_CRC8_INIT;

    if ((p == NULL) && (len != 0u)) {
        return crc;
    }
    for (size_t i = 0; i < len; i++) {
        crc = cm_crc8_update(crc, p[i]);
    }
    return crc;
}

uint16_t cm_crc16_ccitt(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = CM_CRC16_CCITT_INIT;

    if ((p == NULL) && (len != 0u)) {
        return crc;
    }
    for (size_t i = 0; i < len; i++) {
        crc = cm_crc16_ccitt_update(crc, p[i]);
    }
    return crc;
}
