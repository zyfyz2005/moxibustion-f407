/**
 * @file if_input.c
 * @brief 按键输入实现：30 ms 时间窗 + 状态机消抖，只上报按下沿。
 *
 * 为什么要有"候选态"这一层：按键抖动表现为电平在几十毫秒内反复跳。
 * 做法是——电平一变就记下时刻、把它当成候选，等它稳定够久
 * （`debounce_ms`）才承认；承认的那一刻如果是"按下"，就产生一次事件。
 * 全程只读时间、不做延时，所以放在 10 ms 轮询里也不会拖慢任务。
 */

#include "if_input.h"

#include "drv_gpio.h"
#include "cm_time.h"

/** 每个按键一份消抖状态 */
typedef struct {
    bool     stable;        /**< 已确认的按下状态 */
    bool     candidate;     /**< 待确认的状态 */
    uint32_t change_ms;     /**< candidate 最近一次变化的时刻 */
} input_key_state_t;

#define IF_INPUT_KEY_NUM   3u

static input_key_state_t s_key[IF_INPUT_KEY_NUM];
static uint32_t          s_debounce_ms = 30u;

/** 三个按键：读电平的函数 + 对应的事件位 */
static bool (*const s_key_read[IF_INPUT_KEY_NUM])(void) = {
    drv_gpio_key_power_down,
    drv_gpio_key_mute_down,
    drv_gpio_key_reset_down,
};

static const uint32_t s_key_mask[IF_INPUT_KEY_NUM] = {
    IF_INPUT_KEY_POWER,
    IF_INPUT_KEY_MUTE,
    IF_INPUT_KEY_RESET,
};

cm_err_t if_input_init(uint32_t debounce_ms)
{
    s_debounce_ms = (debounce_ms == 0u) ? 1u : debounce_ms;

    for (uint8_t i = 0u; i < IF_INPUT_KEY_NUM; i++) {
        bool raw = s_key_read[i]();

        s_key[i].stable    = raw;
        s_key[i].candidate = raw;
        s_key[i].change_ms = 0u;
        /* 上电时已经按住的键不会被当成"新按下"：下面 poll 只在
         * stable 与 candidate 不一致、且稳定够久时才产生事件 */
    }
    return CM_OK;
}

uint32_t if_input_poll(uint32_t now_ms)
{
    uint32_t events = 0u;

    for (uint8_t i = 0u; i < IF_INPUT_KEY_NUM; i++) {
        bool raw = s_key_read[i]();

        if (raw != s_key[i].candidate) {
            /* 电平变了：记为候选，重新开始计时 */
            s_key[i].candidate = raw;
            s_key[i].change_ms = now_ms;
            continue;
        }

        if ((s_key[i].stable != s_key[i].candidate) &&
            cm_timeout_expired(now_ms, s_key[i].change_ms, s_debounce_ms)) {
            /* 稳定够久了，承认这次变化 */
            s_key[i].stable = s_key[i].candidate;
            if (s_key[i].stable) {
                events |= s_key_mask[i];       /* 只上报按下沿 */
            }
        }
    }
    return events;
}

bool if_input_is_down(uint32_t key_mask)
{
    for (uint8_t i = 0u; i < IF_INPUT_KEY_NUM; i++) {
        if ((key_mask & s_key_mask[i]) != 0u) {
            return s_key[i].stable;
        }
    }
    return false;
}
