/**
 * @file cm_json.h
 * @brief 轻量 JSON：只做本工程需要的那点事，不引三方库。
 *
 * 写（cm_json_w_*）：按固定字段顺序拼一个扁平对象，容量超了置 overflow 标志，不越界写。
 * 读（cm_json_get_*）：在一个**扁平对象**里按键取标量值。嵌套的对象/数组会被整体跳过，
 *                     所以"外层有个 value，里层也有个 value"时只会命中外层那个。
 *
 * 不做的事（明确写在接口上，避免以后误用）：
 *   - 不解析嵌套结构、不返回数组元素
 *   - 不支持 Unicode 转义（\uXXXX 按普通字符跳过）
 *   - 数值只支持整数（本工程没有浮点字段：温度按整数摄氏度上报）
 */

#pragma once

#include "cm_types.h"

/*============================ 拼装 ============================*/

typedef struct {
    char  *buf;       /**< 输出缓冲（调用方提供） */
    size_t cap;       /**< 缓冲总容量，含结尾 '\0' */
    size_t len;       /**< 已写字符数（不含 '\0'） */
    bool   overflow;  /**< true = 中途写不下，结果不可信 */
    bool   first;     /**< 当前对象里还没写过成员 */
} cm_json_w_t;

/**
 * @brief 绑定输出缓冲并清零状态
 * @param w   写上下文（调用方提供，本模块只填它的字段）
 * @param buf 输出缓冲
 * @param cap 缓冲总容量，含结尾 '\0'
 */
void cm_json_w_init(cm_json_w_t *w, char *buf, size_t cap);

/** @brief 写 '{' */
bool cm_json_w_begin_obj(cm_json_w_t *w);

/** @brief 写 '}' */
bool cm_json_w_end_obj(cm_json_w_t *w);

/**
 * @brief 写一个整数成员，如 "temp_c":28
 * @param w     写上下文
 * @param key   成员名（调用方保证是合法 JSON 键名）
 * @param value 成员值
 */
bool cm_json_w_kv_int(cm_json_w_t *w, const char *key, int32_t value);

/**
 * @brief 写一个无符号成员，如 "uptime_s":86400
 * @param w     写上下文
 * @param key   成员名
 * @param value 成员值
 */
bool cm_json_w_kv_u32(cm_json_w_t *w, const char *key, uint32_t value);

/**
 * @brief 写一个字符串成员，如 "state":"COUNTDOWN"（会做必要的转义）
 * @param w     写上下文
 * @param key   成员名
 * @param value 成员值；传 NULL 按空串处理
 */
bool cm_json_w_kv_str(cm_json_w_t *w, const char *key, const char *value);

/** @brief 结果是否可用（没溢出） */
bool cm_json_w_ok(const cm_json_w_t *w);

/** @brief 已写长度（不含 '\0'） */
size_t cm_json_w_len(const cm_json_w_t *w);

/** @brief 取结果指针（永远是合法的 C 字符串，溢出时是被截断的前缀） */
const char *cm_json_w_result(const cm_json_w_t *w);

/*============================ 解析 ============================*/

/** 值的类型 */
typedef enum {
    CM_JSON_V_NONE = 0,  /**< 没找到这个键 */
    CM_JSON_V_NUM,       /**< 数字（本工程按整数用） */
    CM_JSON_V_STR,       /**< 字符串 */
    CM_JSON_V_BOOL,      /**< true / false */
    CM_JSON_V_NULL,      /**< null */
} cm_json_val_e;

/**
 * @brief 在扁平对象里查一个键，返回值的类型、起始指针与长度
 * @param val      值的起始位置（字符串类型时指向引号内的第一个字符）
 * @param val_len  值的字符数（字符串类型时为引号内的字符数，不含转义还原）
 * @return 值的类型；CM_JSON_V_NONE 表示没找到
 */
cm_json_val_e cm_json_find(const char *json, size_t len, const char *key,
                           const char **val, size_t *val_len);

/**
 * @brief 按键取整数
 * @return true = 找到且是能装进 int32 的整数
 */
bool cm_json_get_i32(const char *json, size_t len, const char *key, int32_t *out);

/**
 * @brief 按键取无符号整数（负数视为失败）
 */
bool cm_json_get_u32(const char *json, size_t len, const char *key, uint32_t *out);

/**
 * @brief 按键取字符串（本工程只需要短字符串，如 "set_threshold"）
 * @param out 输出缓冲，保证 '\0' 结尾
 * @param cap 输出缓冲容量
 * @return true = 找到且是字符串且装得下
 */
bool cm_json_get_str(const char *json, size_t len, const char *key, char *out, size_t cap);
