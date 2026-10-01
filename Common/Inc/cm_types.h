/**
 * @file cm_types.h
 * @brief 公共基础类型与宏。不依赖任何硬件、不依赖 FreeRTOS。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*============================ 常用宏 ============================*/

/** 取数组元素个数 */
#define CM_ARRAY_LEN(a)        (sizeof(a) / sizeof((a)[0]))

/** 取较小 / 较大值 */
#define CM_MIN(a, b)           (((a) < (b)) ? (a) : (b))
#define CM_MAX(a, b)           (((a) > (b)) ? (a) : (b))

/** 把变量限制在 [lo, hi] 区间内 */
#define CM_CLAMP(v, lo, hi)    (CM_MIN(CM_MAX((v), (lo)), (hi)))

/** 显式标记"这个参数暂时不用"，避免编译器告警 */
#define CM_UNUSED(x)           ((void)(x))

/**
 * @brief 编译期断言
 * @note C11 用 _Static_assert；老编译器退化成故意制造非法数组长度。
 *       tag 必须是合法标识符，且同一文件内不能重名。
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define CM_STATIC_ASSERT(cond, tag)  _Static_assert((cond), #tag)
#else
#define CM_STATIC_ASSERT(cond, tag)  typedef char cm_static_assert_##tag[(cond) ? 1 : -1]
#endif

/*============================ 统一错误码 ============================*/

/**
 * @brief 全工程统一的返回值
 * @note 负数是错误，0 是成功。各层都用这个类型，避免每层自造一套。
 */
typedef enum {
    CM_OK          =  0,   /**< 成功 */
    CM_ERR_PARAM   = -1,   /**< 参数非法 */
    CM_ERR_TIMEOUT = -2,   /**< 超时 */
    CM_ERR_IO      = -3,   /**< 底层读写失败 */
    CM_ERR_CRC     = -4,   /**< 校验失败 */
    CM_ERR_FULL    = -5,   /**< 缓冲/队列满 */
    CM_ERR_EMPTY   = -6,   /**< 缓冲/队列空 */
    CM_ERR_BUSY    = -7,   /**< 忙 */
    CM_ERR_NOMEM   = -8,   /**< 内存不足 */
    CM_ERR_STATE   = -9,   /**< 状态不允许该操作 */
    CM_ERR_NOTSUP  = -10,  /**< 不支持 */
} cm_err_t;

/*============================ 字节序小工具 ============================*/

/** 小端拆装：用于日志/缓存记录等需要固定字节序的场合 */
static inline void cm_put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline uint16_t cm_get_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline void cm_put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline uint32_t cm_get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}
