/**
 * @file app_fsm.c
 * @brief 状态机实现
 */

#include "app_fsm.h"
#include "app_cfg.h"

void app_fsm_init(MoxaState_t *s, const moxa_cfg_t *cfg)
{
    if ((s == NULL) || (cfg == NULL)) {
        return;
    }
    s->th_max_c        = cfg->th_max_c;
    s->duration_s      = cfg->duration_s;
    s->remain_s        = cfg->duration_s;
    s->state           = (uint8_t)MOXA_ST_WAITING;
    s->switch_on       = 0u;
    s->muted           = 0u;
    s->temp_alarm      = 0u;
    s->countdown_alarm = 0u;
}

void app_fsm_start(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }
    s->switch_on = 1u;
    s->remain_s  = s->duration_s;
    s->muted     = 0u;
    s->state     = (uint8_t)MOXA_ST_COUNTDOWN;
}

void app_fsm_stop(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }
    s->switch_on       = 0u;
    s->muted           = 0u;
    s->remain_s        = s->duration_s;
    s->temp_alarm      = 0u;
    s->countdown_alarm = 0u;
    s->state           = (uint8_t)MOXA_ST_WAITING;
}

void app_fsm_reset(MoxaState_t *s)
{
    /* 复位与关机在当前设计下等价：都回 WAITING、清静音、重装倒计时。
     * 区别只在语义（复位强调"从报警里退出"），实现上合并减少分支 */
    app_fsm_stop(s);
}

void app_fsm_power_key(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }
    if (s->state == (uint8_t)MOXA_ST_WAITING) {
        app_fsm_start(s);
    } else {
        app_fsm_stop(s);
    }
}

void app_fsm_mute_key(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }
    /* 只在 ALARM 期间可置位（计划书第四节）；其他状态下按键无效果 */
    if (s->state == (uint8_t)MOXA_ST_ALARM) {
        s->muted = 1u;
    }
}

void app_fsm_tick_1s(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }
    if ((s->switch_on == 0u) || (s->state == (uint8_t)MOXA_ST_WAITING)) {
        return;
    }
    if (s->remain_s > 0u) {
        s->remain_s--;
    }
}

void app_fsm_set_temp(MoxaState_t *s, bool valid, int16_t temp_c, uint16_t humi_pct)
{
    if (s == NULL) {
        return;
    }
    s->temp_valid = valid ? 1u : 0u;
    if (valid) {
        s->temp_c   = temp_c;
        s->humi_pct = humi_pct;
    }
    /* 读失败时保留上一次的温度值，但 temp_valid=0 ——
     * 报警判定要求 temp_valid，所以失败周期不会误撤掉温度报警 */
}

void app_fsm_set_threshold(MoxaState_t *s, uint16_t th_max_c)
{
    if (s != NULL) {
        s->th_max_c = th_max_c;
    }
}

void app_fsm_set_duration(MoxaState_t *s, uint32_t duration_s)
{
    if (s == NULL) {
        return;
    }
    s->duration_s = duration_s;
    if (s->state == (uint8_t)MOXA_ST_WAITING) {
        s->remain_s = duration_s;    /* 待机时改时长，下次开机用的就是这个新值 */
    }
}

void app_fsm_eval(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }

    /* 温度报警要求温度有效；读失败的那个周期按"条件不成立"算，
     * 但也不会因此退出 ALARM——退出需要"两个条件同时解除"，
     * 而归零报警一旦成立就不会自己解除 */
    s->temp_alarm      = ((s->temp_valid != 0u) && (s->temp_c >= (int16_t)s->th_max_c)) ? 1u : 0u;
    s->countdown_alarm = ((s->switch_on != 0u) && (s->remain_s == 0u)) ? 1u : 0u;

    switch ((moxa_state_e)s->state) {
    case MOXA_ST_WAITING:
        /* 待机不该有报警条件成立 */
        s->temp_alarm      = 0u;
        s->countdown_alarm = 0u;
        break;

    case MOXA_ST_COUNTDOWN:
        if ((s->temp_alarm != 0u) || (s->countdown_alarm != 0u)) {
            s->state = (uint8_t)MOXA_ST_ALARM;
        }
        break;

    case MOXA_ST_ALARM:
        if ((s->temp_alarm == 0u) && (s->countdown_alarm == 0u)) {
            /* 两个条件同时解除 → 回到倒计时；同时清除静音标志 */
            s->muted = 0u;
            s->state = (uint8_t)MOXA_ST_COUNTDOWN;
        }
        break;

    default:
        s->state = (uint8_t)MOXA_ST_WAITING;
        break;
    }
}
