/**
 * @file drv_uart.c
 * @brief 调试口（阻塞）+ 模组口（DMA/空闲中断）实现
 *
 * 本文件同时实现了三个 HAL 的弱回调（不需要在头文件里声明，HAL 已经声明过）：
 *   HAL_UARTEx_RxEventCallback —— 空闲/半满/满 事件，搬数据
 *   HAL_UART_TxCpltCallback    —— 模组口一次 DMA 发送完成
 *   HAL_UART_ErrorCallback     —— 出错后把接收重新拉起来
 */

#include "drv_uart.h"

/*============================ 调试口 ============================*/

static UART_HandleTypeDef *s_debug_uart = NULL;

cm_err_t drv_uart_debug_init(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return CM_ERR_PARAM;
    }
    s_debug_uart = huart;
    return CM_OK;
}

int drv_uart_debug_write(const uint8_t *data, uint16_t len, void *user)
{
    CM_UNUSED(user);

    if ((s_debug_uart == NULL) || (data == NULL) || (len == 0u)) {
        return 0;
    }
    if (HAL_UART_Transmit(s_debug_uart, (uint8_t *)data, len, DRV_UART_DEBUG_TIMEOUT_MS) != HAL_OK) {
        return 0;
    }
    return (int)len;
}

/*============================ 模组口 ============================*/

static UART_HandleTypeDef *s_net_uart = NULL;

/* DMA 直写的缓冲：硬件在 Circular 模式下一直往这里写 */
static uint8_t s_dma_rx_buf[DRV_UART_NET_DMA_BUF_SIZE];

/* 中断搬、任务读的环形缓冲 */
static uint8_t       s_net_rx_storage[DRV_UART_NET_RING_SIZE];
static cm_ringbuf_t  s_net_rx;

/* DMA 上次被搬到的位置（相对缓冲起点） */
static volatile uint16_t s_dma_last_pos = 0u;

static volatile uint32_t s_rx_total   = 0u;
static volatile uint32_t s_rx_dropped = 0u;

/* 发送暂存与忙标志 */
static uint8_t        s_tx_buf[DRV_UART_NET_TX_BUF_SIZE];
static volatile bool  s_tx_busy = false;

static bool s_rx_started = false;

/**
 * @brief 把 DMA 缓冲里"从上次位置到现在"的字节搬进环形缓冲
 * @note 只由中断和任务各一处调用，且两边都不嵌套，所以不加临界区
 */
static void uart_net_drain_dma(void)
{
    uint16_t counter;
    uint16_t pos;
    uint16_t last;
    uint16_t written;

    if ((s_net_uart == NULL) || (s_net_uart->hdmarx == NULL)) {
        return;
    }

    /* NDTR 是"还差多少字节写满缓冲"，反算就是当前写位置 */
    counter = (uint16_t)__HAL_DMA_GET_COUNTER(s_net_uart->hdmarx);
    if (counter > DRV_UART_NET_DMA_BUF_SIZE) {
        counter = (uint16_t)DRV_UART_NET_DMA_BUF_SIZE;
    }
    pos  = (uint16_t)(DRV_UART_NET_DMA_BUF_SIZE - counter);
    last = s_dma_last_pos;

    if (pos == last) {
        return;
    }

    if (pos > last) {
        written = cm_ringbuf_write(&s_net_rx, &s_dma_rx_buf[last], (uint16_t)(pos - last));
        s_rx_total += written;
        s_rx_dropped += (uint32_t)((uint16_t)(pos - last) - written);
    } else {
        /* 绕回了：先搬 [last, 末尾)，再搬 [0, pos) */
        uint16_t tail_len = (uint16_t)(DRV_UART_NET_DMA_BUF_SIZE - last);

        written = cm_ringbuf_write(&s_net_rx, &s_dma_rx_buf[last], tail_len);
        s_rx_total += written;
        s_rx_dropped += (uint32_t)(tail_len - written);

        if (pos > 0u) {
            written = cm_ringbuf_write(&s_net_rx, &s_dma_rx_buf[0], pos);
            s_rx_total += written;
            s_rx_dropped += (uint32_t)(pos - written);
        }
    }

    s_dma_last_pos = pos;
}

cm_err_t drv_uart_net_init(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return CM_ERR_PARAM;
    }

    s_net_uart = huart;
    cm_ringbuf_init(&s_net_rx, s_net_rx_storage, DRV_UART_NET_RING_SIZE);
    s_dma_last_pos = 0u;
    s_rx_total     = 0u;
    s_rx_dropped   = 0u;
    s_tx_busy      = false;

    drv_uart_net_restart_rx();
    if (!s_rx_started) {
        return CM_ERR_IO;
    }
    return CM_OK;
}

void drv_uart_net_restart_rx(void)
{
    if ((s_net_uart == NULL) || (s_net_uart->hdmarx == NULL)) {
        s_rx_started = false;
        return;
    }

    if (HAL_UARTEx_ReceiveToIdle_DMA(s_net_uart, s_dma_rx_buf, DRV_UART_NET_DMA_BUF_SIZE) == HAL_OK) {
        /* HAL 默认会把半满中断也打开，这里关掉：只在空闲/满的时候搬，少一半中断 */
        __HAL_DMA_DISABLE_IT(s_net_uart->hdmarx, DMA_IT_HT);
        s_dma_last_pos = 0u;
        s_rx_started   = true;
    } else {
        s_rx_started = false;
    }
}

void drv_uart_net_poll(void)
{
    if (!s_rx_started) {
        return;
    }
    uart_net_drain_dma();
}

uint16_t drv_uart_net_available(void)
{
    return cm_ringbuf_count(&s_net_rx);
}

uint16_t drv_uart_net_read(uint8_t *dst, uint16_t len)
{
    return cm_ringbuf_read(&s_net_rx, dst, len);
}

uint16_t drv_uart_net_peek(uint8_t *dst, uint16_t len)
{
    return cm_ringbuf_peek(&s_net_rx, dst, len);
}

uint16_t drv_uart_net_drop(uint16_t len)
{
    return cm_ringbuf_drop(&s_net_rx, len);
}

uint32_t drv_uart_net_rx_total(void)
{
    return s_rx_total;
}

uint32_t drv_uart_net_rx_dropped(void)
{
    return s_rx_dropped;
}

cm_err_t drv_uart_net_write(const uint8_t *data, uint16_t len)
{
    if ((s_net_uart == NULL) || (data == NULL) || (len == 0u)) {
        return CM_ERR_PARAM;
    }
    if (len > DRV_UART_NET_TX_BUF_SIZE) {
        return CM_ERR_PARAM;
    }
    if (s_tx_busy) {
        return CM_ERR_BUSY;
    }

    for (uint16_t i = 0u; i < len; i++) {
        s_tx_buf[i] = data[i];
    }

    s_tx_busy = true;
    if (HAL_UART_Transmit_DMA(s_net_uart, s_tx_buf, len) != HAL_OK) {
        s_tx_busy = false;
        return CM_ERR_IO;
    }
    return CM_OK;
}

bool drv_uart_net_tx_busy(void)
{
    return s_tx_busy;
}

/*============================ HAL 回调 ============================*/

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    CM_UNUSED(Size);     /* 用 DMA 位置算，不用这个参数，见文件头说明 */

    if (huart == s_net_uart) {
        uart_net_drain_dma();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == s_net_uart) {
        s_tx_busy = false;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_net_uart) {
        return;
    }
    /* 出错时 HAL 会把 DMA 接收停掉，这里把它重新拉起来，否则串口从此哑掉 */
    drv_uart_net_restart_rx();
}
