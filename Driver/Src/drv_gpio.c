/**
 * @file drv_gpio.c
 * @brief 按键 / LED / 模组使能的引脚读写实现
 */

#include "drv_gpio.h"

cm_err_t drv_gpio_init(void)
{
    /* 共阳 LED：写高 = 灭。CubeMX 生成的是低电平，这里纠正过来 */
    HAL_GPIO_WritePin(DRV_GPIO_LED0_PORT, DRV_GPIO_LED0_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(DRV_GPIO_LED1_PORT, DRV_GPIO_LED1_PIN, GPIO_PIN_SET);

    /* 模组使能：上电就得拉高，否则 ESP8266 根本不启动、AT 一条都不会回 */
    HAL_GPIO_WritePin(DRV_GPIO_MODULE_EN_PORT, DRV_GPIO_MODULE_EN_PIN, GPIO_PIN_SET);

    return CM_OK;
}

bool drv_gpio_key_power_down(void)
{
    /* WK_UP 接 VCC3.3，按下为高；平时靠 MCU 内部下拉保持低 */
    return (HAL_GPIO_ReadPin(DRV_GPIO_KEY_POWER_PORT, DRV_GPIO_KEY_POWER_PIN) == GPIO_PIN_SET);
}

bool drv_gpio_key_mute_down(void)
{
    /* KEY1 接 GND，按下为低；平时靠 MCU 内部上拉保持高 */
    return (HAL_GPIO_ReadPin(DRV_GPIO_KEY_MUTE_PORT, DRV_GPIO_KEY_MUTE_PIN) == GPIO_PIN_RESET);
}

bool drv_gpio_key_reset_down(void)
{
    /* KEY0 接 GND，按下为低 */
    return (HAL_GPIO_ReadPin(DRV_GPIO_KEY_RESET_PORT, DRV_GPIO_KEY_RESET_PIN) == GPIO_PIN_RESET);
}

void drv_gpio_led_set(drv_gpio_led_e led, bool on)
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState level;

    if (led == DRV_GPIO_LED0) {
        port = DRV_GPIO_LED0_PORT;
        pin  = DRV_GPIO_LED0_PIN;
    } else {
        port = DRV_GPIO_LED1_PORT;
        pin  = DRV_GPIO_LED1_PIN;
    }

    /* 共阳：要点亮就得把引脚拉低 */
    level = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, level);
}

void drv_gpio_module_enable(bool on)
{
    HAL_GPIO_WritePin(DRV_GPIO_MODULE_EN_PORT, DRV_GPIO_MODULE_EN_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool drv_gpio_module_led_high(void)
{
    return (HAL_GPIO_ReadPin(DRV_GPIO_MODULE_LED_PORT, DRV_GPIO_MODULE_LED_PIN) == GPIO_PIN_SET);
}
