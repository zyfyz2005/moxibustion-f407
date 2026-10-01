/**
 * @file drv_24c02.c
 * @brief 24C02 读写实现（CRC 由 if_store 负责，这里只管字节搬运）
 */

#include "drv_24c02.h"

static I2C_HandleTypeDef *s_ee_i2c = NULL;

cm_err_t drv_24c02_init(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return CM_ERR_PARAM;
    }
    s_ee_i2c = hi2c;
    return CM_OK;
}

bool drv_24c02_is_ready(void)
{
    if (s_ee_i2c == NULL) {
        return false;
    }
    return (HAL_I2C_IsDeviceReady(s_ee_i2c, DRV_24C02_ADDR_8BIT, 2u, DRV_24C02_TIMEOUT_MS) == HAL_OK);
}

cm_err_t drv_24c02_read(uint8_t mem_addr, uint8_t *dst, uint16_t len)
{
    if ((s_ee_i2c == NULL) || (dst == NULL) || (len == 0u)) {
        return CM_ERR_PARAM;
    }

    if (HAL_I2C_Mem_Read(s_ee_i2c, DRV_24C02_ADDR_8BIT, mem_addr,
                         I2C_MEMADD_SIZE_8BIT, dst, len, DRV_24C02_TIMEOUT_MS) != HAL_OK) {
        return CM_ERR_IO;
    }
    return CM_OK;
}

cm_err_t drv_24c02_write(uint8_t mem_addr, const uint8_t *src, uint16_t len,
                         drv_24c02_page_fn on_page_done, void *user)
{
    uint16_t written = 0u;

    if ((s_ee_i2c == NULL) || (src == NULL) || (len == 0u)) {
        return CM_ERR_PARAM;
    }

    while (written < len) {
        uint8_t  addr       = (uint8_t)(mem_addr + written);
        uint8_t  offset_in_page = (uint8_t)(addr % DRV_24C02_PAGE_SIZE);
        uint16_t chunk      = (uint16_t)(DRV_24C02_PAGE_SIZE - offset_in_page);

        /* 不能跨页：本页剩余空间不足就把这次写截到页边界 */
        if (chunk > (uint16_t)(len - written)) {
            chunk = (uint16_t)(len - written);
        }

        if (HAL_I2C_Mem_Write(s_ee_i2c, DRV_24C02_ADDR_8BIT, addr,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)&src[written], chunk,
                              DRV_24C02_TIMEOUT_MS) != HAL_OK) {
            return CM_ERR_IO;
        }

        /* 等内部写周期：这段时间器件不响应总线，下一页必须等它写完 */
        HAL_Delay(DRV_24C02_WRITE_CYCLE_MS);

        written = (uint16_t)(written + chunk);
        if (on_page_done != NULL) {
            on_page_done(user);
        }
    }
    return CM_OK;
}
