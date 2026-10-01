/**
 * @file cm_ringbuf.h
 * @brief 字节环形缓冲。串口 DMA + IDLE 接收、AT 响应扫描都靠它。
 *
 * 使用约束（决定了它不用加锁）：
 *   - 单生产者、单消费者（SPSC）。典型用法是"中断里写、任务里读"。
 *   - head/tail 是 16 位，Cortex-M4 上 16 位对齐读写是原子的，不需要临界区。
 *   - 容量上限 65535 字节；实际size按 2 的幂给（取模会编译成位与，快）。
 *
 * 满/空的判定：head == tail 表示空，因此最多存 size-1 个字节。
 */

#pragma once

#include "cm_types.h"

typedef struct {
    uint8_t         *buf;    /**< 外部提供的存储区，本模块不做动态分配 */
    uint16_t         size;   /**< 存储区字节数，建议 2 的幂 */
    volatile uint16_t head;  /**< 写指针：只由生产者改 */
    volatile uint16_t tail;  /**< 读指针：只由消费者改 */
} cm_ringbuf_t;

/**
 * @brief 绑定存储区并清空
 * @param rb      缓冲对象
 * @param storage 外部存储区（本模块不做动态分配）
 * @param size    存储区字节数，建议 2 的幂；小于 2 时忽略本次初始化
 */
void cm_ringbuf_init(cm_ringbuf_t *rb, uint8_t *storage, uint16_t size);

/**
 * @brief 当前已存字节数
 * @param rb 缓冲对象
 */
uint16_t cm_ringbuf_count(const cm_ringbuf_t *rb);

/**
 * @brief 还剩多少字节可写
 * @param rb 缓冲对象
 */
uint16_t cm_ringbuf_space(const cm_ringbuf_t *rb);

/** @brief 是否为空 */
static inline bool cm_ringbuf_empty(const cm_ringbuf_t *rb)
{
    return (cm_ringbuf_count(rb) == 0u);
}

/** @brief 清空（生产者侧的复位，别在中断与任务同时调） */
void cm_ringbuf_reset(cm_ringbuf_t *rb);

/**
 * @brief 写入，写不下就截断（不覆盖老数据）
 * @param rb  缓冲对象
 * @param src 源数据
 * @param len 想写入的字节数
 * @return 实际写入的字节数
 */
uint16_t cm_ringbuf_write(cm_ringbuf_t *rb, const uint8_t *src, uint16_t len);

/**
 * @brief 只看不取，用于 AT 响应"先扫完整帧再决定消费多少"
 * @param rb  缓冲对象
 * @param dst 输出缓冲
 * @param len 想拷贝的最大字节数
 * @return 实际拷贝的字节数
 */
uint16_t cm_ringbuf_peek(const cm_ringbuf_t *rb, uint8_t *dst, uint16_t len);

/**
 * @brief 从尾部丢弃 len 字节（配合 peek 完成消费）
 * @param rb  缓冲对象
 * @param len 想丢弃的最大字节数
 * @return 实际丢弃的字节数
 */
uint16_t cm_ringbuf_drop(cm_ringbuf_t *rb, uint16_t len);

/**
 * @brief 取出并丢弃
 * @param rb  缓冲对象
 * @param dst 输出缓冲
 * @param len 想取出的最大字节数
 * @return 实际拷贝的字节数
 */
uint16_t cm_ringbuf_read(cm_ringbuf_t *rb, uint8_t *dst, uint16_t len);

/**
 * @brief 在已存数据里找第一个出现的字节
 * @param rb   缓冲对象
 * @param byte 要找的字节
 * @param from  从第几个字节开始找（0 = 从头）
 * @param pos   命中时写下标（相对缓冲起点，不是内存地址）
 * @return true = 找到
 */
bool cm_ringbuf_find(const cm_ringbuf_t *rb, uint8_t byte, uint16_t from, uint16_t *pos);
