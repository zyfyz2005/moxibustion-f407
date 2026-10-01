/**
 * @file drv_uart.h
 * @brief 两个串口通道的封装：调试口（USART1）与模组口（USART3）。
 *
 * 两个口用途不同，实现也不同，别混着看：
 *
 *   USART1（调试口，PA9/PA10 + 板载 CH340）
 *     只用于打日志，**阻塞发送**。代价要说清楚：按 115200 波特率，
 *     一条 200 字节的日志要占住 CPU 约 17 ms。当前只在事件发生和
 *     每 30 s 的自检行里打印，可以接受；以后觉得占时间就给它加 TX DMA
 *     （需要在 CubeMX 里补一路 DMA，M1 清单里已经留了这句话）。
 *
 *   USART3（模组口，PB10/PB11 + ESP8266）
 *     接收用 **DMA 环形 + 空闲中断**：HAL 的 `HAL_UARTEx_ReceiveToIdle_DMA()`
 *     会在串口空闲时回调 `HAL_UARTEx_RxEventCallback()`，本驱动在该回调里
 *     把 DMA 缓冲里新增的字节搬进内部环形缓冲。这样**不需要改 CubeMX 生成的
 *     `stm32f4xx_it.c`**——HAL 已经把它接好了。
 *     发送用 DMA（Normal 模式），内部有暂存缓冲，调用方不必关心缓冲生命周期。
 *
 * 搬迁是"位置驱动"的：按 DMA 的写位置（NDTR 反算）算新增了多少字节，
 * 不依赖 HAL 回调里 `Size` 参数的具体语义——HAL 各版本对循环模式下的
 * 这个参数解释不完全一致，用位置算最稳。`drv_uart_net_poll()` 是同一套
 * 搬迁逻辑的兜底入口，NetTask 定期调一次即可，不怕漏中断。
 */

#pragma once

#include "cm_types.h"

#include "stm32f4xx_hal.h"

#include "cm_ringbuf.h"

/*============================ 缓冲尺寸 ============================*/

/** DMA 直写的环形缓冲（硬件按 Circular 模式一直往里写） */
#define DRV_UART_NET_DMA_BUF_SIZE   512u
/** 中断搬进、任务读出的环形缓冲 */
#define DRV_UART_NET_RING_SIZE      512u
/** 发送暂存：DMA 发送期间这块内存不能被改写，所以由驱动自己持有 */
#define DRV_UART_NET_TX_BUF_SIZE    256u
/** 调试口阻塞发送的超时，单位 ms */
#define DRV_UART_DEBUG_TIMEOUT_MS   100u

/*============================ 调试口（USART1）============================*/

/**
 * @brief 绑定调试串口句柄
 * @param huart CubeMX 生成的句柄（`huart1`）
 */
cm_err_t drv_uart_debug_init(UART_HandleTypeDef *huart);

/**
 * @brief 阻塞写一段数据（供 cm_log 的写口回调使用，签名与 cm_log_write_fn 一致）
 * @param data 待写数据
 * @param len  字节数
 * @param user 未使用
 * @return 实际写出的字节数；失败返回 0
 */
int drv_uart_debug_write(const uint8_t *data, uint16_t len, void *user);

/*============================ 模组口（USART3）============================*/

/**
 * @brief 绑定模组串口并启动 DMA 环形接收
 * @param huart CubeMX 生成的句柄（`huart3`，其 hdmarx/hdmatx 已由 MspInit 链接好）
 * @return CM_OK；CM_ERR_PARAM / CM_ERR_IO（启动接收失败）
 */
cm_err_t drv_uart_net_init(UART_HandleTypeDef *huart);

/**
 * @brief 兜底搬迁：把 DMA 缓冲里的新增字节搬进环形缓冲
 * @note 中断里已经搬过一次，这个入口是给任务侧定期调用的兜底；
 *       在调度器启动前调用会直接返回（DMA 还没开始工作）
 */
void drv_uart_net_poll(void);

/** @brief 环形缓冲里现在有多少字节可读 */
uint16_t drv_uart_net_available(void);

/** @brief 取走数据（先 peek 再 drop） */
uint16_t drv_uart_net_read(uint8_t *dst, uint16_t len);

/** @brief 只看不取 */
uint16_t drv_uart_net_peek(uint8_t *dst, uint16_t len);

/** @brief 丢弃已读走的字节 */
uint16_t drv_uart_net_drop(uint16_t len);

/** @brief 累计从 DMA 搬走的字节数（M1 自检"收到字节不丢"就看它） */
uint32_t drv_uart_net_rx_total(void);

/** @brief 因环形缓冲满而丢掉的字节数 */
uint32_t drv_uart_net_rx_dropped(void);

/**
 * @brief 通过 DMA 发送一段数据
 * @param data 待发数据（内部会拷进驱动的暂存缓冲，调用方可以立刻释放）
 * @param len  字节数，不得超过 DRV_UART_NET_TX_BUF_SIZE
 * @return CM_OK 已启动发送；CM_ERR_BUSY 上一次还没发完；CM_ERR_PARAM 参数非法或太长
 */
cm_err_t drv_uart_net_write(const uint8_t *data, uint16_t len);

/** @brief 上一次 DMA 发送是否还没完成 */
bool drv_uart_net_tx_busy(void);

/** @brief 主动重新启动接收（出错恢复用；正常路径不需要调） */
void drv_uart_net_restart_rx(void);
