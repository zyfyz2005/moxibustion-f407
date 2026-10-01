/**
 * @file cm_log.c
 * @brief 调试串口日志实现
 */

#include "cm_log.h"

#include <stdarg.h>
#include <stdio.h>

static cm_log_write_fn s_write  = NULL;
static cm_log_lock_fn  s_lock   = NULL;
static cm_log_lock_fn  s_unlock = NULL;
static void           *s_user   = NULL;

static cm_log_time_fn  s_time      = NULL;
static void           *s_time_user = NULL;

/* 格式化缓冲是全局静态的：一是避免放任务栈上把栈撑爆（NetTask 才 4KB），
 * 二是统一由 s_lock/s_unlock 串行化，不会出现两条日志互相插入 */
static char s_buf[CM_LOG_BUF_SIZE];

void cm_log_init(cm_log_write_fn write, cm_log_lock_fn lock, cm_log_lock_fn unlock, void *user)
{
    s_write  = write;
    s_lock   = lock;
    s_unlock = unlock;
    s_user   = user;
}

void cm_log_set_time_source(cm_log_time_fn fn, void *user)
{
    s_time      = fn;
    s_time_user = user;
}

bool cm_log_is_ready(void)
{
    return (s_write != NULL);
}

void cm_log_write_raw(const uint8_t *data, uint16_t len)
{
    if ((s_write == NULL) || (data == NULL) || (len == 0u)) {
        return;
    }
    if (s_lock != NULL) {
        s_lock(s_user);
    }
    (void)s_write(data, len, s_user);
    if (s_unlock != NULL) {
        s_unlock(s_user);
    }
}

void cm_log_printf(const char *fmt, ...)
{
    int      format_len;
    size_t   prefix;
    uint32_t uptime_sec = 0u;
    va_list  ap;

    if ((s_write == NULL) || (fmt == NULL)) {
        return;
    }

    if (s_time != NULL) {
        uptime_sec = s_time(s_time_user);
    }

    /* 前缀与正文分两步格式化：前缀长度固定且短，正文用掉剩下的容量 */
    prefix = (size_t)snprintf(s_buf, sizeof(s_buf), "[t=%lus] ", (unsigned long)uptime_sec);
    if (prefix >= sizeof(s_buf)) {
        prefix = sizeof(s_buf) - 1u;
    }

    va_start(ap, fmt);
    format_len = vsnprintf(&s_buf[prefix], sizeof(s_buf) - prefix, fmt, ap);
    va_end(ap);

    if (format_len < 0) {
        s_buf[prefix] = '\0';
        format_len = 0;
    }

    /* 补结尾换行：截断时也要保证不会写到缓冲之外 */
    {
        size_t used = prefix + (size_t)format_len;
        if (used > (sizeof(s_buf) - 3u)) {
            used = sizeof(s_buf) - 3u;
        }
        s_buf[used]     = '\r';
        s_buf[used + 1] = '\n';
        s_buf[used + 2] = '\0';

        if (s_lock != NULL) {
            s_lock(s_user);
        }
        (void)s_write((const uint8_t *)s_buf, (uint16_t)(used + 2u), s_user);
        if (s_unlock != NULL) {
            s_unlock(s_user);
        }
    }
}
