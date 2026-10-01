/**
 * @file app_ctx.h
 * @brief 应用层全局上下文：队列 / 事件组 / 互斥量句柄、共享状态、配置 token、喂狗钩子。
 *
 * 这里的东西全部在调度器启动前创建（app_ctx_init），运行期只被引用、不再增删。
 * 运行期禁止 malloc —— 所有缓冲都是静态的。
 */

#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"

#include "app_types.h"

/*============================ 句柄 ============================*/

extern QueueHandle_t      q_sensor;     /**< SensorTask → ControlTask */
extern QueueHandle_t      q_cmd;        /**< NetTask    → ControlTask */
extern QueueHandle_t      q_ack;        /**< ControlTask→ NetTask */
extern QueueHandle_t      q_cfg_req;    /**< ControlTask→ ConfigTask（深度 1） */
extern QueueHandle_t      q_cfg_resp;   /**< ConfigTask → ControlTask（深度 2） */
extern QueueHandle_t      q_at_tx;      /**< NetTask 自产自销的待发队列 */

extern EventGroupHandle_t eg_state;     /**< 状态变更事件组 */

extern SemaphoreHandle_t  mtx_state;    /**< 保护 g_moxa_state */
extern SemaphoreHandle_t  mtx_config;   /**< 保护 I2C1（运行期唯一持有者 ConfigTask） */
extern SemaphoreHandle_t  mtx_dbg_uart; /**< 保护调试串口，防日志交错 */

/*============================ 共享状态 ============================*/

/**
 * @brief 创建队列 / 事件组 / 互斥量，并用已加载的配置初始化共享状态。
 * @param cfg 从 24C02 读出的配置（读失败时内部已填默认值）
 * @note 必须在 osKernelStart() 之前调用：用的是 xQueueCreate 这类"可以在调度器前调用"
 *       的 API，且不能有阻塞等待
 */
cm_err_t app_ctx_init(const moxa_cfg_t *cfg);

/**
 * @brief 取状态快照（加锁拷贝）
 * @note 消费者（AlarmTask / NetTask）都用它，不要直接读 g_moxa_state
 */
void app_state_take(MoxaState_t *out);

/**
 * @brief 提交状态（**只有 ControlTask 可以调**）
 * @note 内部动作：seq 自增 → 加锁拷贝 → 解锁 → 广播 APP_EVT_ALL_STATE
 */
void app_state_commit(MoxaState_t *s);

/** @brief 当前状态序号（诊断用） */
uint32_t app_state_seq(void);

/**
 * @brief 取下一个配置 token
 * @note 设备侧自有的 16 位单调计数器，回绕时跳过 0；**只有 ControlTask 调用**，
 *       因此不需要加锁。用它匹配配置回执，而不是用客户端的 msg_id——
 *       客户端可能复用 msg_id，撞上迟到的响应就会串到新请求上。
 */
uint16_t app_token_next(void);

/*============================ 喂狗钩子 ============================*/

/**
 * @brief 喂狗函数原型
 * @note 由 main.c 注册 HAL_IWDG_Refresh 的包装，Application 层不直接调 HAL
 */
typedef void (*app_wdt_feed_fn)(void);

/** @brief 注册喂狗函数（未注册时 app_wdt_feed 为空操作） */
void app_wdt_set_feed(app_wdt_feed_fn fn);

/** @brief 喂一次狗；IWDG 未使能时也是空操作 */
void app_wdt_feed(void);

/*============================ 时间 ============================*/

/**
 * @brief 当前毫秒时基
 * @note FreeRTOS tick 在 configTICK_RATE_HZ = 1000 下就是 1ms 一格。
 *       统一从这里取时间，避免各任务自己写一遍换算。
 */
uint32_t app_now_ms(void);

/**
 * @brief 更新 uptime 秒（ControlTask 每秒调一次）
 * @note uptime 的权威值在 MoxaState_t.uptime_s 里；这里放一份镜像，
 *       是给日志时间戳用的——cm_log 不能去加 state_mutex，
 *       否则在临界区里打印日志就可能自锁。
 */
void app_uptime_set(uint32_t sec);

/** @brief 取 uptime 秒（日志时间戳用） */
uint32_t app_uptime_get(void);

/*============================ 错误计数 ============================*/

/**
 * 错误分类。计划书第六节要求"分别计数，汇总进 err_cnt"，
 * 所以这里分类累加，遥测里的 err_cnt 报总数。
 */
typedef enum {
    APP_ERR_AT = 0,      /**< AT 超时 / 失败 */
    APP_ERR_MQTT,        /**< MQTT 连接或发布失败 */
    APP_ERR_JSON,        /**< JSON 组装/解析失败 */
    APP_ERR_CFG_IO,      /**< 配置读写失败 */
    APP_ERR_QDROP,       /**< 队列满丢弃 */
    APP_ERR_TOKEN,       /**< 配置 token 不匹配（迟到响应） */
    APP_ERR_BADCMD,      /**< 非法指令 */
    APP_ERR_NO_MSGID,    /**< 下行缺失 msg_id */
    APP_ERR_COUNT,       /**< 分类数目（不是错误） */
} app_err_id_e;

/**
 * @brief 某个错误计数 +1
 * @note 任何任务都可以调；内部用临界区保护，不会丢计数
 */
void app_err_inc(app_err_id_e id);

/** @brief 取某个分类的计数 */
uint16_t app_err_get(app_err_id_e id);

/** @brief 全部错误的总数（遥测里的 err_cnt 用这个） */
uint16_t app_err_total(void);

/*============================ 日志 ============================*/

/**
 * @brief 把 cm_log 的写口接到 USART1 和 mtx_dbg_uart 上。
 * @note 实现依赖 Driver 层 drv_uart（第 4 步随驱动一起补）。
 *       在此之前所有日志静默丢弃，不影响纯逻辑验证。
 */
void app_log_init(void);
