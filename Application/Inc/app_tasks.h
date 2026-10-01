/**
 * @file app_tasks.h
 * @brief 6 个 FreeRTOS 任务的入口与创建函数。
 *
 * 优先级 / 栈深 / 任务名一律取自 app_cfg.h，main.c 里只调一次 app_tasks_create()，
 * 这样"任务表"只有一处定义，不会出现 main.c 和配置文件两边数字不一致。
 */

#pragma once

#include "app_ctx.h"     /* 带出 FreeRTOS 头与 TaskHandle_t */

/** 任务信息（诊断打印用） */
typedef struct {
    const char  *name;
    TaskHandle_t handle;
} app_task_info_t;

/**
 * @brief 创建全部 6 个任务并初始化心跳表
 * @note 在 osKernelStart() 之前调用
 */
cm_err_t app_tasks_create(void);

/**
 * @brief 取任务信息表
 * @param count 输出任务个数（可为 NULL）
 */
const app_task_info_t *app_tasks_info(uint8_t *count);

/* 任务入口。签名必须是 void(*)(void*)，与 xTaskCreate 的 TaskFunction_t 一致 */
void task_dog(void *arg);
void task_control(void *arg);
void task_alarm(void *arg);
void task_sensor(void *arg);
void task_net(void *arg);
void task_config(void *arg);
