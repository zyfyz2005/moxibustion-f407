/**
 * @file cm_ringbuf.c
 * @brief 字节环形缓冲实现（SPSC，无锁）
 */

#include "cm_ringbuf.h"

void cm_ringbuf_init(cm_ringbuf_t *rb, uint8_t *storage, uint16_t size)
{
    if ((rb == NULL) || (storage == NULL) || (size < 2u)) {
        return;
    }
    rb->buf  = storage;
    rb->size = size;
    rb->head = 0u;
    rb->tail = 0u;
}

uint16_t cm_ringbuf_count(const cm_ringbuf_t *rb)
{
    uint16_t head;
    uint16_t tail;

    if ((rb == NULL) || (rb->buf == NULL)) {
        return 0u;
    }
    /* head/tail 由两端各自更新，先各读一次再看差值，避免读到"半个新值" */
    head = rb->head;
    tail = rb->tail;
    if (head >= tail) {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(rb->size - tail + head);
}

uint16_t cm_ringbuf_space(const cm_ringbuf_t *rb)
{
    if ((rb == NULL) || (rb->buf == NULL) || (rb->size < 2u)) {
        return 0u;
    }
    return (uint16_t)(rb->size - 1u - cm_ringbuf_count(rb));
}

void cm_ringbuf_reset(cm_ringbuf_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->head = 0u;
    rb->tail = 0u;
}

uint16_t cm_ringbuf_write(cm_ringbuf_t *rb, const uint8_t *src, uint16_t len)
{
    uint16_t space;
    uint16_t count;

    if ((rb == NULL) || (rb->buf == NULL) || (src == NULL) || (len == 0u)) {
        return 0u;
    }

    space = cm_ringbuf_space(rb);
    count = (len < space) ? len : space;

    for (uint16_t i = 0u; i < count; i++) {
        rb->buf[rb->head] = src[i];
        rb->head = (uint16_t)((rb->head + 1u) % rb->size);
    }
    return count;
}

uint16_t cm_ringbuf_peek(const cm_ringbuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t available;
    uint16_t copy_count;
    uint16_t index;

    if ((rb == NULL) || (rb->buf == NULL) || (dst == NULL) || (len == 0u)) {
        return 0u;
    }

    available  = cm_ringbuf_count(rb);
    copy_count = (len < available) ? len : available;
    index = rb->tail;

    for (uint16_t i = 0u; i < copy_count; i++) {
        dst[i] = rb->buf[index];
        index = (uint16_t)((index + 1u) % rb->size);
    }
    return copy_count;
}

uint16_t cm_ringbuf_drop(cm_ringbuf_t *rb, uint16_t len)
{
    uint16_t available;
    uint16_t drop_count;

    if ((rb == NULL) || (rb->buf == NULL) || (len == 0u)) {
        return 0u;
    }

    available  = cm_ringbuf_count(rb);
    drop_count = (len < available) ? len : available;
    rb->tail = (uint16_t)((rb->tail + drop_count) % rb->size);
    return drop_count;
}

uint16_t cm_ringbuf_read(cm_ringbuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t count = cm_ringbuf_peek(rb, dst, len);
    (void)cm_ringbuf_drop(rb, count);
    return count;
}

bool cm_ringbuf_find(const cm_ringbuf_t *rb, uint8_t byte, uint16_t from, uint16_t *pos)
{
    uint16_t count;
    uint16_t index;

    if ((rb == NULL) || (rb->buf == NULL)) {
        return false;
    }

    count = cm_ringbuf_count(rb);
    if (from >= count) {
        return false;
    }

    index = (uint16_t)((rb->tail + from) % rb->size);
    for (uint16_t i = from; i < count; i++) {
        if (rb->buf[index] == byte) {
            if (pos != NULL) {
                *pos = i;
            }
            return true;
        }
        index = (uint16_t)((index + 1u) % rb->size);
    }
    return false;
}
