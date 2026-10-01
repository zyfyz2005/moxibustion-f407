/**
 * @file app_tasks.c
 * @brief 任务创建
 */

#include "app_tasks.h"
#include "app_cfg.h"
#include "app_heartbeat.h"

#define APP_TASK_COUNT  6u

/* 顺序与诊断打印顺序一致：dog 在最前，方便一眼看到看门狗自己的栈 */
static app_task_info_t s_tasks[APP_TASK_COUNT];

/** 建一个任务并把句柄记进表里 */
static bool tasks_create_one(uint8_t index, TaskFunction_t task_entry, const char *name,
                             uint16_t stack_words, UBaseType_t priority)
{
    BaseType_t result;

    s_tasks[index].name   = name;
    s_tasks[index].handle = NULL;

    result = xTaskCreate(task_entry, name, stack_words, NULL, priority, &s_tasks[index].handle);
    return (result == pdPASS);
}

cm_err_t app_tasks_create(void)
{
    bool ok = true;

    ok = ok && tasks_create_one(0u, task_dog,     APP_TASK_NAME_DOG,     APP_STACK_DOG,     APP_PRIO_DOG);
    ok = ok && tasks_create_one(1u, task_control, APP_TASK_NAME_CONTROL, APP_STACK_CONTROL, APP_PRIO_CONTROL);
    ok = ok && tasks_create_one(2u, task_alarm,   APP_TASK_NAME_ALARM,   APP_STACK_ALARM,   APP_PRIO_ALARM);
    ok = ok && tasks_create_one(3u, task_sensor,  APP_TASK_NAME_SENSOR,  APP_STACK_SENSOR,  APP_PRIO_SENSOR);
    ok = ok && tasks_create_one(4u, task_net,     APP_TASK_NAME_NET,     APP_STACK_NET,     APP_PRIO_NET);
    ok = ok && tasks_create_one(5u, task_config,  APP_TASK_NAME_CONFIG,  APP_STACK_CONFIG,  APP_PRIO_CONFIG);

    /* 心跳表以 0 时刻为基准；调度器起来后各任务会立刻各自推进一次 */
    app_hb_init(0u);

    return ok ? CM_OK : CM_ERR_NOMEM;
}

const app_task_info_t *app_tasks_info(uint8_t *count)
{
    if (count != NULL) {
        *count = (uint8_t)APP_TASK_COUNT;
    }
    return s_tasks;
}
