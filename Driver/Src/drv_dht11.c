/**
 * @file drv_dht11.c
 * @brief DHT11 单总线时序实现（位级延时用 DWT->CYCCNT）
 */

#include "drv_dht11.h"

/** 每微秒的 CYCCNT 增量（SystemCoreClock 在 168 MHz 下是 168） */
static uint32_t s_cycles_per_us = 168u;

static inline bool dht11_line_high(void)
{
    return (HAL_GPIO_ReadPin(DRV_DHT11_PORT, DRV_DHT11_PIN) == GPIO_PIN_SET);
}

/** 忙等指定微秒数（只用于 DHT11 的位时隙，不用于长延时） */
static void dht11_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * s_cycles_per_us;

    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
        /* 空转 */
    }
}

/** 在 timeout_us 内等线变成指定电平；超时返回 false */
static bool dht11_wait_level(bool high, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = timeout_us * s_cycles_per_us;

    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
        if (dht11_line_high() == high) {
            return true;
        }
    }
    return false;
}

cm_err_t drv_dht11_init(void)
{
    /* 打开 DWT 的周期计数器：CoreDebug 使能跟踪，然后放行 CYCCNT */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    s_cycles_per_us = SystemCoreClock / 1000000u;
    if (s_cycles_per_us == 0u) {
        s_cycles_per_us = 1u;
    }

    /* 释放数据线（写 1 = 高阻，外部 10K 拉高） */
    HAL_GPIO_WritePin(DRV_DHT11_PORT, DRV_DHT11_PIN, GPIO_PIN_SET);
    return CM_OK;
}

cm_err_t drv_dht11_read(int16_t *temp_c, uint16_t *humi_pct)
{
    uint8_t data[5] = { 0u, 0u, 0u, 0u, 0u };
    uint8_t checksum;

    if ((temp_c == NULL) || (humi_pct == NULL)) {
        return CM_ERR_PARAM;
    }

    /* 1) 主机起始信号：拉低 18ms，然后释放 */
    HAL_GPIO_WritePin(DRV_DHT11_PORT, DRV_DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(DRV_DHT11_START_LOW_MS);
    HAL_GPIO_WritePin(DRV_DHT11_PORT, DRV_DHT11_PIN, GPIO_PIN_SET);

    /* 2) 等从机响应：先 80µs 低，再 80µs 高。
     *    这里的两个 wait 同时也是"器件在不在"的判据：没响应就是没插好或线太长 */
    if (!dht11_wait_level(false, 100u)) {
        return CM_ERR_TIMEOUT;
    }
    if (!dht11_wait_level(true, 100u)) {
        return CM_ERR_TIMEOUT;
    }

    /* 3) 收 40 bit：每位先 50µs 低电平，然后高电平——26~28µs 是 0，70µs 是 1。
     *    在低电平结束后的第 40µs 采样，正好把 0 和 1 分开 */
    for (uint8_t i = 0u; i < 40u; i++) {
        if (!dht11_wait_level(false, 100u)) {   /* 等这一位开始（线变低） */
            return CM_ERR_TIMEOUT;
        }
        if (!dht11_wait_level(true, 100u)) {    /* 等 50µs 低电平结束 */
            return CM_ERR_TIMEOUT;
        }
        dht11_delay_us(40u);
        if (dht11_line_high()) {
            data[i / 8u] |= (uint8_t)(1u << (7u - (i % 8u)));
        }
    }

    /* 4) 校验和：前 4 字节之和的低 8 位 */
    checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return CM_ERR_CRC;
    }

    /* DHT11 的小数位固定为 0，这里只取整数部分 */
    *humi_pct = data[0];
    *temp_c   = (int16_t)data[2];
    return CM_OK;
}
