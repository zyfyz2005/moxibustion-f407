/**
 * @file drv_gpio.h
 * @brief 按键电平、LED、模组使能的引脚读写。
 *
 * 引脚映射集中写在下面这一组宏里：CubeMX 生成的 `main.h` 没有引脚标签宏
 * （这块工程里除 PC13 外都没起名字），所以"哪只脚接什么"这件事由驱动层自己声明一份。
 * 换板子只改这一段，别的层一行不动。
 *
 * 引脚事实来源：计划书 v11 第三节 + `docs/20261001-原理图信息核对清单.md`。
 * 注意 LED 是**共阳**：低电平点亮，写高才是灭。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

/*============================ 引脚映射 ============================*/

#define DRV_GPIO_KEY_POWER_PORT     GPIOA           /**< PA0  WK_UP，按下为高 */
#define DRV_GPIO_KEY_POWER_PIN      GPIO_PIN_0
#define DRV_GPIO_KEY_MUTE_PORT      GPIOE           /**< PE3  KEY1，按下为低 */
#define DRV_GPIO_KEY_MUTE_PIN       GPIO_PIN_3
#define DRV_GPIO_KEY_RESET_PORT     GPIOE           /**< PE4  KEY0，按下为低 */
#define DRV_GPIO_KEY_RESET_PIN      GPIO_PIN_4

#define DRV_GPIO_LED0_PORT          GPIOF           /**< PF9  DS0，共阳（低电平点亮） */
#define DRV_GPIO_LED0_PIN           GPIO_PIN_9
#define DRV_GPIO_LED1_PORT          GPIOF           /**< PF10 DS1，共阳（低电平点亮） */
#define DRV_GPIO_LED1_PIN           GPIO_PIN_10

#define DRV_GPIO_MODULE_EN_PORT     GPIOF           /**< PF6  ESP8266 CH_PD，高电平使能 */
#define DRV_GPIO_MODULE_EN_PIN      GPIO_PIN_6
#define DRV_GPIO_MODULE_LED_PORT    GPIOC           /**< PC0  模组 GPIO16 状态灯（读电平） */
#define DRV_GPIO_MODULE_LED_PIN     GPIO_PIN_0

/** 板上两颗灯 */
typedef enum {
    DRV_GPIO_LED0 = 0,      /**< DS0 / PF9：状态灯（COUNTDOWN 期间常亮） */
    DRV_GPIO_LED1,          /**< DS1 / PF10：报警灯（跟蜂鸣器闪） */
} drv_gpio_led_e;

/*============================ 接口 ============================*/

/**
 * @brief 驱动层初始化：把 LED 熄灭、模组使能拉高
 * @note 必须调：CubeMX 生成的 `MX_GPIO_Init()` 把 PF9/PF10 初始化成了**低电平**，
 *       在共阳板上等于"开机两颗灯全亮"，所以这里要显式写高把它们关掉
 */
cm_err_t drv_gpio_init(void);

/** @brief 开关机键是否按下（未消抖；消抖在 if_input 里做） */
bool drv_gpio_key_power_down(void);

/** @brief 静音键是否按下（未消抖） */
bool drv_gpio_key_mute_down(void);

/** @brief 复位键是否按下（未消抖） */
bool drv_gpio_key_reset_down(void);

/**
 * @brief 点亮 / 熄灭一颗灯
 * @param led 哪颗灯
 * @param on  true = 点亮（共阳板内部会写成低电平）
 */
void drv_gpio_led_set(drv_gpio_led_e led, bool on);

/**
 * @brief 控制模组使能脚 CH_PD
 * @param on true = 拉高使能模组；false = 拉低，模组断电不工作
 */
void drv_gpio_module_enable(bool on);

/** @brief 读模组状态灯 GPIO16 的电平（true = 高） */
bool drv_gpio_module_led_high(void);
