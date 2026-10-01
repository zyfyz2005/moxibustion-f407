/**
 * @file cm_time.c
 * @brief cm_time 里唯一需要 .c 的部分：秒计数器
 */

#include "cm_time.h"

bool cm_sec_counter_add(cm_sec_counter_t *c, uint32_t delta_ms)
{
    uint32_t carry;

    if (c == NULL) {
        return false;
    }

    c->acc_ms += delta_ms;
    if (c->acc_ms < 1000u) {
        return false;
    }

    /* 用除法而不是循环减 1000：调用周期抖动（比如被高优先级任务拖到 12ms）时
     * 也能一次补上，秒数不会累积漂移 */
    carry = c->acc_ms / 1000u;
    c->acc_ms -= carry * 1000u;
    c->sec += carry;
    return true;
}
