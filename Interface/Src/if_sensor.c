/**
 * @file if_sensor.c
 * @brief 温湿度采集服务：包一层 DHT11，并维护验收要用的失败统计。
 *
 * 统计的口径（对着计划书的验收标准）：
 *   total      每次调用算一个采集周期，含失败
 *   fail       校验和错、器件没响应都算失败
 *   consec     当前连续失败次数（成功即清零）
 *   max_consec 历史最大连续失败次数——验收标准 ≤2 看的就是它
 */

#include "if_sensor.h"

#include "drv_dht11.h"

static if_sensor_stat_t s_stat;

cm_err_t if_sensor_init(void)
{
    s_stat.total      = 0u;
    s_stat.fail       = 0u;
    s_stat.consec     = 0u;
    s_stat.max_consec = 0u;
    return drv_dht11_init();
}

cm_err_t if_sensor_read(int16_t *temp_c, uint16_t *humi_pct)
{
    cm_err_t result;

    s_stat.total++;
    result = drv_dht11_read(temp_c, humi_pct);

    if (result == CM_OK) {
        s_stat.consec = 0u;
    } else {
        s_stat.fail++;
        s_stat.consec++;
        if (s_stat.consec > s_stat.max_consec) {
            s_stat.max_consec = s_stat.consec;
        }
    }
    return result;
}

const if_sensor_stat_t *if_sensor_get_stat(void)
{
    return &s_stat;
}

void if_sensor_reset_stat(void)
{
    s_stat.total      = 0u;
    s_stat.fail       = 0u;
    s_stat.consec     = 0u;
    s_stat.max_consec = 0u;
}
