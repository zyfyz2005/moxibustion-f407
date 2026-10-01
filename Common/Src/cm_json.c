/**
 * @file cm_json.ch
 * @brief 轻量 JSON 实现
 *
 * 解析器是"扫描器"而不是"构造 AST 的解析器"：只认最外层对象里的键值对，
 * 遇到嵌套结构就整段跳过。理由是本工程的下行报文就是一个扁平对象
 * （{"msg_id":17,"cmd":"set_threshold","value":45}），
 * 为此引一个通用 JSON 库属于杀鸡用牛刀。
 */

#include "cm_json.h"

/*============================ 拼装 ============================*/

void cm_json_w_init(cm_json_w_t *w, char *buf, size_t cap)
{
    if (w == NULL) {
        return;
    }
    w->buf      = buf;
    w->cap      = cap;
    w->len      = 0u;
    w->overflow = (buf == NULL) || (cap == 0u);
    w->first    = true;
    if ((buf != NULL) && (cap > 0u)) {
        buf[0] = '\0';
    }
}

static bool json_putc(cm_json_w_t *w, char ch)
{
    if ((w == NULL) || w->overflow) {
        return false;
    }
    /* 留一个字节给结尾 '\0' */
    if ((w->len + 1u) >= w->cap) {
        w->overflow = true;
        w->buf[w->len] = '\0';
        return false;
    }
    w->buf[w->len] = ch;
    w->len++;
    w->buf[w->len] = '\0';
    return true;
}

static bool json_puts(cm_json_w_t *w, const char *s)
{
    if (s == NULL) {
        s = "";
    }
    for (size_t i = 0u; s[i] != '\0'; i++) {
        if (!json_putc(w, s[i])) {
            return false;
        }
    }
    return true;
}

bool cm_json_w_begin_obj(cm_json_w_t *w)
{
    w->first = true;
    return json_putc(w, '{');
}

bool cm_json_w_end_obj(cm_json_w_t *w)
{
    return json_putc(w, '}');
}

/** 写成员名的引号与冒号，并在不是首个成员时补逗号 */
static bool json_kv_head(cm_json_w_t *w, const char *key)
{
    if (!w->first) {
        if (!json_putc(w, ',')) {
            return false;
        }
    }
    w->first = false;
    return json_putc(w, '"') && json_puts(w, key) && json_putc(w, '"') && json_putc(w, ':');
}

bool cm_json_w_kv_str(cm_json_w_t *w, const char *key, const char *value)
{
    if ((w == NULL) || (key == NULL)) {
        return false;
    }
    if (value == NULL) {
        value = "";
    }
    if (!json_kv_head(w, key) || !json_putc(w, '"')) {
        return false;
    }
    for (size_t i = 0u; value[i] != '\0'; i++) {
        char ch = value[i];
        /* 只转义 JSON 语法上必须转义的几个字符。本工程的字符串都是宏里写死的
         * （状态名、主题名），值里不该出现控制字符，出现了也要保证不破坏报文 */
        switch (ch) {
        case '"':  if (!json_puts(w, "\\\"")) { return false; } break;
        case '\\': if (!json_puts(w, "\\\\")) { return false; } break;
        case '\n': if (!json_puts(w, "\\n"))  { return false; } break;
        case '\r': if (!json_puts(w, "\\r"))  { return false; } break;
        case '\t': if (!json_puts(w, "\\t"))  { return false; } break;
        default:   if (!json_putc(w, ch))      { return false; } break;
        }
    }
    return json_putc(w, '"');
}

/** 无符号数转十进制追加；v 为 0 时输出一个 '0' */
static bool json_puts_u32(cm_json_w_t *w, uint32_t v)
{
    char tmp[10];
    size_t count = 0u;

    do {
        tmp[count] = (char)('0' + (v % 10u));
        v /= 10u;
        count++;
    } while ((v != 0u) && (count < sizeof(tmp)));

    while (count > 0u) {
        count--;
        if (!json_putc(w, tmp[count])) {
            return false;
        }
    }
    return true;
}

bool cm_json_w_kv_int(cm_json_w_t *w, const char *key, int32_t value)
{
    if ((w == NULL) || (key == NULL)) {
        return false;
    }
    if (!json_kv_head(w, key)) {
        return false;
    }
    if (value < 0) {
        if (!json_putc(w, '-')) {
            return false;
        }
        /* 先转成 uint32 再取反，避免 INT32_MIN 取负溢出 */
        return json_puts_u32(w, (uint32_t)(-(int64_t)value));
    }
    return json_puts_u32(w, (uint32_t)value);
}

bool cm_json_w_kv_u32(cm_json_w_t *w, const char *key, uint32_t value)
{
    if ((w == NULL) || (key == NULL)) {
        return false;
    }
    if (!json_kv_head(w, key)) {
        return false;
    }
    return json_puts_u32(w, value);
}

bool cm_json_w_ok(const cm_json_w_t *w)
{
    return (w != NULL) && (!w->overflow) && (w->buf != NULL);
}

size_t cm_json_w_len(const cm_json_w_t *w)
{
    return (w == NULL) ? 0u : w->len;
}

const char *cm_json_w_result(const cm_json_w_t *w)
{
    if ((w == NULL) || (w->buf == NULL)) {
        return "";
    }
    return w->buf;
}

/*============================ 解析 ============================*/

static const char *skip_ws(const char *s, const char *end)
{
    while ((s < end) && ((*s == ' ') || (*s == '\t') || (*s == '\r') || (*s == '\n'))) {
        s++;
    }
    return s;
}

/** 跳过一段字符串（s 指向开引号），返回闭引号之后的位置；不合法则返回 NULL */
static const char *skip_string(const char *s, const char *end)
{
    if ((s >= end) || (*s != '"')) {
        return NULL;
    }
    s++;
    while (s < end) {
        if (*s == '\\') {
            /* 转义符：连它后面那个字符一起跳过，\uXXXX 也当两个字符跳过 */
            s += 2;
            continue;
        }
        if (*s == '"') {
            return s + 1;
        }
        s++;
    }
    return NULL;
}

/** 跳过任意一个值（字符串 / 数字 / true / false / null / 嵌套对象或数组） */
static const char *skip_value(const char *s, const char *end)
{
    int depth = 0;

    s = skip_ws(s, end);
    if (s >= end) {
        return NULL;
    }
    if (*s == '"') {
        return skip_string(s, end);
    }

    while (s < end) {
        char ch = *s;
        if ((ch == '{') || (ch == '[')) {
            depth++;
        } else if ((ch == '}') || (ch == ']')) {
            if (depth == 0) {
                return s;   /* 到了外层对象的收尾，当前值到此结束 */
            }
            depth--;
            if (depth == 0) {
                return s + 1;
            }
        } else if ((depth == 0) && ((ch == ',') || (ch == '"'))) {
            return s;       /* 撞到分隔符 / 下一个键，当前值到此结束 */
        } else if ((depth == 0) && (ch == ':')) {
            return s;       /* 理论上不该出现，按结束处理，保证不越界 */
        }
        s++;
    }
    return s;
}

/** 比较键名是否等于 key（大小写敏感） */
static bool json_key_equals(const char *s, const char *end, const char *key)
{
    size_t i = 0u;      /* s、end、key 三者同步推进的下标 */

    for (;;) {
        if (key[i] == '\0') {
            return ((s + i) >= end);    /* key 用完且键名也正好用完 → 相等 */
        }
        if ((s + i) >= end) {
            return false;               /* 键名比 key 短 */
        }
        if (s[i] != key[i]) {
            return false;               /* 键名含转义或字符不同，一律不匹配 */
        }
        i++;
    }
}

cm_json_val_e cm_json_find(const char *json, size_t len, const char *key,
                           const char **val, size_t *val_len)
{
    const char *s;
    const char *end;
    const char *p;

    if (val != NULL)     { *val = NULL; }
    if (val_len != NULL) { *val_len = 0u; }
    if ((json == NULL) || (key == NULL) || (len == 0u)) {
        return CM_JSON_V_NONE;
    }

    end = json + len;
    s = skip_ws(json, end);
    if ((s >= end) || (*s != '{')) {
        return CM_JSON_V_NONE;      /* 只处理扁平对象 */
    }
    s++;

    for (;;) {
        const char *key_start;
        const char *key_end;
        const char *v_start;
        const char *v_end;
        cm_json_val_e type = CM_JSON_V_NONE;

        s = skip_ws(s, end);
        if ((s >= end) || (*s == '}')) {
            return CM_JSON_V_NONE;
        }
        if (*s != '"') {
            return CM_JSON_V_NONE;
        }
        key_start = s + 1;
        p = skip_string(s, end);
        if (p == NULL) {
            return CM_JSON_V_NONE;
        }
        key_end = p - 1;

        s = skip_ws(p, end);
        if ((s >= end) || (*s != ':')) {
            return CM_JSON_V_NONE;
        }
        s = skip_ws(s + 1, end);
        if (s >= end) {
            return CM_JSON_V_NONE;
        }
        v_start = s;

        if (*s == '"') {
            type = CM_JSON_V_STR;
            v_start = s + 1;
            p = skip_string(s, end);
            if (p == NULL) {
                return CM_JSON_V_NONE;
            }
            v_end = p - 1;
        } else if ((*s == 't') || (*s == 'f')) {
            type = CM_JSON_V_BOOL;
            p = skip_value(s, end);
            if (p == NULL) {
                return CM_JSON_V_NONE;
            }
            v_end = p;
        } else if (*s == 'n') {
            type = CM_JSON_V_NULL;
            p = skip_value(s, end);
            if (p == NULL) {
                return CM_JSON_V_NONE;
            }
            v_end = p;
        } else {
            type = CM_JSON_V_NUM;
            p = skip_value(s, end);
            if (p == NULL) {
                return CM_JSON_V_NONE;
            }
            v_end = p;
        }

        if (json_key_equals(key_start, key_end, key)) {
            if (val != NULL)     { *val = v_start; }
            if (val_len != NULL) { *val_len = (size_t)(v_end - v_start); }
            return type;
        }

        s = skip_ws(p, end);
        if ((s < end) && (*s == ',')) {
            s++;
            continue;
        }
        return CM_JSON_V_NONE;
    }
}

/** 把 [s, end) 里的十进制整数解析成 int32；多余字符（如小数部分）视为失败 */
static bool json_parse_i64(const char *s, const char *end, int64_t *out)
{
    int64_t accumulator = 0;
    bool    is_negative = false;
    bool    has_digit = false;

    if ((s == NULL) || (end == NULL) || (s >= end)) {
        return false;
    }
    if (*s == '-') {
        is_negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (s < end) {
        char ch = *s;
        if ((ch < '0') || (ch > '9')) {
            return false;
        }
        accumulator = (accumulator * 10) + (int64_t)(ch - '0');
        if ((accumulator > 2147483647LL) || (accumulator < -2147483648LL)) {
            return false;
        }
        has_digit = true;
        s++;
    }
    if (!has_digit) {
        return false;
    }
    *out = is_negative ? -accumulator : accumulator;
    return true;
}

bool cm_json_get_i32(const char *json, size_t len, const char *key, int32_t *out)
{
    const char   *val;
    size_t        vlen;
    int64_t       tmp;
    cm_json_val_e t;

    if (out == NULL) {
        return false;
    }
    t = cm_json_find(json, len, key, &val, &vlen);
    if (t != CM_JSON_V_NUM) {
        return false;
    }
    if (!json_parse_i64(val, val + vlen, &tmp)) {
        return false;
    }
    *out = (int32_t)tmp;
    return true;
}

bool cm_json_get_u32(const char *json, size_t len, const char *key, uint32_t *out)
{
    int32_t tmp;

    if (out == NULL) {
        return false;
    }
    if (!cm_json_get_i32(json, len, key, &tmp) || (tmp < 0)) {
        return false;
    }
    *out = (uint32_t)tmp;
    return true;
}

bool cm_json_get_str(const char *json, size_t len, const char *key, char *out, size_t cap)
{
    const char   *val;
    size_t        vlen;
    cm_json_val_e t;

    if ((out == NULL) || (cap == 0u)) {
        return false;
    }
    out[0] = '\0';

    t = cm_json_find(json, len, key, &val, &vlen);
    if (t != CM_JSON_V_STR) {
        return false;
    }
    if (vlen >= cap) {
        return false;
    }
    for (size_t i = 0u; i < vlen; i++) {
        /* 键值都是宏里写死的短标识，不需要还原转义；遇到反斜杠直接按字面收，
         * 上层做字符串比较时不匹配即视为非法指令 */
        out[i] = val[i];
    }
    out[vlen] = '\0';
    return true;
}
