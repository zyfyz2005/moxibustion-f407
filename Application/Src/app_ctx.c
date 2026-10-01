/**
 * @file app_ctx.c
 * @brief 应用层全局上下文实现
 */

#include "app_ctx.h"
#include "app_cfg.h"

#include "cm_log.h"

#include "if_log.h"

#include "usart.h"      /* huart1：调试串口句柄（CubeMX 生成） */

/*============================ 句柄定义 ============================*/

QueueHandle_t      q_sensor   = NULL;
QueueHandle_t      q_cmd      = NULL;
QueueHandle_t      q_ack      = NULL;
QueueHandle_t      q_cfg_req  = NULL;
QueueHandle_t      q_cfg_resp = NULL;
QueueHandle_t      q_at_tx    = NULL;

EventGroupHandle_t eg_state   = NULL;

SemaphoreHandle_t  mtx_state    = NULL;
SemaphoreHandle_t  mtx_config   = NULL;
SemaphoreHandle_t  mtx_dbg_uart = NULL;

/*============================ 共享状态 ============================*/

/* 只允许 ControlTask 写入；其他任务一律走 app_state_take() */
static MoxaState_t s_state;

/* 配置 token：设备侧自有，回绕跳过 0 */
static uint16_t    s_token = 1u;

/* 喂狗钩子 */
static app_wdt_feed_fn s_wdt_feed = NULL;

/* uptime 镜像（日志时间戳用） */
static volatile uint32_t s_uptime_s = 0u;

/* 错误分类计数 */
static volatile uint16_t s_err[APP_ERR_COUNT];

/*============================ 初始化 ============================*/

/** 建一个队列；失败就记下来统一报错，避免每处都写 if */
static bool ctx_create_queues(void)
{
    q_sensor   = xQueueCreate(APP_Q_SENSOR_LEN,   sizeof(SensorSample_t));
    q_cmd      = xQueueCreate(APP_Q_CMD_LEN,      sizeof(CmdMsg_t));
    q_ack      = xQueueCreate(APP_Q_ACK_LEN,      sizeof(AckMsg_t));
    q_cfg_req  = xQueueCreate(APP_Q_CFG_REQ_LEN,  sizeof(ConfigReq_t));
    q_cfg_resp = xQueueCreate(APP_Q_CFG_RESP_LEN, sizeof(ConfigResp_t));
    q_at_tx    = xQueueCreate(APP_Q_AT_TX_LEN,    sizeof(AtTxItem_t));

    return (q_sensor != NULL) && (q_cmd != NULL) && (q_ack != NULL) &&
           (q_cfg_req != NULL) && (q_cfg_resp != NULL) && (q_at_tx != NULL);
}

cm_err_t app_ctx_init(const moxa_cfg_t *cfg)
{
    if (cfg == NULL) {
        return CM_ERR_PARAM;
    }

    if (!ctx_create_queues()) {
        return CM_ERR_NOMEM;     /* 堆不够：configTOTAL_HEAP_SIZE 太小 */
    }

    eg_state     = xEventGroupCreate();
    mtx_state    = xSemaphoreCreateMutex();
    mtx_config   = xSemaphoreCreateMutex();
    mtx_dbg_uart = xSemaphoreCreateMutex();

    if ((eg_state == NULL) || (mtx_state == NULL) ||
        (mtx_config == NULL) || (mtx_dbg_uart == NULL)) {
        return CM_ERR_NOMEM;
    }

    /* 初始状态：待机、未开机、配置来自 24C02 */
    s_state.state           = (uint8_t)MOXA_ST_WAITING;
    s_state.muted           = 0u;
    s_state.switch_on       = 0u;
    s_state.temp_valid      = 0u;
    s_state.temp_c          = 0;
    s_state.humi_pct        = 0u;
    s_state.remain_s        = cfg->duration_s;
    s_state.th_max_c        = cfg->th_max_c;
    s_state.duration_s      = cfg->duration_s;
    s_state.temp_alarm      = 0u;
    s_state.countdown_alarm = 0u;
    s_state.uptime_s        = 0u;
    s_state.err_cnt         = 0u;
    s_state.seq             = 0u;

    s_token = 1u;

    /* 事件位清零，避免事件组初始值带来一个假唤醒 */
    (void)xEventGroupClearBits(eg_state, APP_EVT_ALL_STATE);

    return CM_OK;
}

/*============================ 共享状态访问 ============================*/

void app_state_take(MoxaState_t *out)
{
    if (out == NULL) {
        return;
    }
    if (xSemaphoreTake(mtx_state, portMAX_DELAY) == pdTRUE) {
        *out = s_state;
        (void)xSemaphoreGive(mtx_state);
        return;
    }
    /* 理论上到不了这里（portMAX_DELAY 只会在调度器没起来时失败） */
    *out = s_state;
}

void app_state_commit(MoxaState_t *s)
{
    if (s == NULL) {
        return;
    }

    s->seq++;    /* 每次变更自增，消费者据此判断是否真有新数据 */

    if (xSemaphoreTake(mtx_state, portMAX_DELAY) == pdTRUE) {
        s_state = *s;
        (void)xSemaphoreGive(mtx_state);
    }

    /* 广播给两个消费者：各自用专属位、pdTRUE 清自己的位 */
    (void)xEventGroupSetBits(eg_state, APP_EVT_ALL_STATE);
}

uint32_t app_state_seq(void)
{
    uint32_t seq;

    if (xSemaphoreTake(mtx_state, portMAX_DELAY) == pdTRUE) {
        seq = s_state.seq;
        (void)xSemaphoreGive(mtx_state);
        return seq;
    }
    return s_state.seq;
}

uint16_t app_token_next(void)
{
    s_token++;
    if (s_token == 0u) {
        s_token = 1u;        /* 回绕跳过 0：0 保留给"未分配" */
    }
    return s_token;
}

/*============================ 喂狗 ============================*/

void app_wdt_set_feed(app_wdt_feed_fn fn)
{
    s_wdt_feed = fn;
}

void app_wdt_feed(void)
{
    if (s_wdt_feed != NULL) {
        s_wdt_feed();
    }
}

/*============================ 日志 ============================*/

/**
 * @brief 日志加锁
 * @note 两点必须判断：① 互斥量还没创建（app_ctx_init 之前）不能拿；
 *       ② 调度器没启动时拿互斥量会触发 configASSERT（死循环），
 *       而开机早期就可能打日志，所以先问一句调度器状态
 */
static void log_lock(void *user)
{
    CM_UNUSED(user);

    if ((mtx_dbg_uart != NULL) &&
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreTake(mtx_dbg_uart, portMAX_DELAY);
    }
}

static void log_unlock(void *user)
{
    CM_UNUSED(user);

    if ((mtx_dbg_uart != NULL) &&
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreGive(mtx_dbg_uart);
    }
}

/** @brief 日志时间戳来源：uptime 秒（ControlTask 每秒刷新的镜像） */
static uint32_t log_time_source(void *user)
{
    CM_UNUSED(user);
    return s_uptime_s;
}

void app_log_init(void)
{
    if (if_log_init(&huart1, log_lock, log_unlock, NULL) != CM_OK) {
        return;     /* 接不上就一直静默丢弃日志，不影响功能 */
    }
    cm_log_set_time_source(log_time_source, NULL);
}

/*============================ 时间 ============================*/

uint32_t app_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * (TickType_t)portTICK_PERIOD_MS);
}

void app_uptime_set(uint32_t sec)
{
    s_uptime_s = sec;
}

uint32_t app_uptime_get(void)
{
    return s_uptime_s;
}

/*============================ 错误计数 ============================*/

void app_err_inc(app_err_id_e id)
{
    if ((uint8_t)id >= (uint8_t)APP_ERR_COUNT) {
        return;
    }
    /* 临界区很短（一次读改写），且 err_cnt 是 16 位，正常跑不会溢出。
     * 用临界区而不是互斥量：本函数可能在任何任务里被调用，
     * 包括不该阻塞的地方 */
    taskENTER_CRITICAL();
    s_err[id]++;
    taskEXIT_CRITICAL();
}

uint16_t app_err_get(app_err_id_e id)
{
    if ((uint8_t)id >= (uint8_t)APP_ERR_COUNT) {
        return 0u;
    }
    return s_err[id];
}

uint16_t app_err_total(void)
{
    uint32_t total = 0u;

    for (uint8_t i = 0u; i < (uint8_t)APP_ERR_COUNT; i++) {
        total += s_err[i];
    }
    return (uint16_t)total;
}

/*============================ FreeRTOS 钩子 ============================*/

/**
 * @brief 栈溢出钩子（configCHECK_FOR_STACK_OVERFLOW = 2）
 * @note M1 出口自检里"故意把某任务栈设成 128 word，看能不能触发本钩子"就靠它。
 *       这里不做恢复——栈已经越界了，继续跑没有意义，打印完停住等看门狗或人工复位。
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    CM_UNUSED(xTask);
    cm_log_printf("!! stack overflow: %s", (pcTaskName != NULL) ? pcTaskName : "?");
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* 停在这里：看门狗（M5 之后）会复位，调试时也能从调用栈直接定位 */
    }
}

/**
 * @brief 堆分配失败钩子（configUSE_MALLOC_FAILED_HOOK = 1）
 * @note 本工程只在启动阶段创建任务/队列时用堆，运行期不 malloc。
 *       真在这里触发说明 configTOTAL_HEAP_SIZE 给小了。
 */
void vApplicationMallocFailedHook(void)
{
    cm_log_printf("!! malloc failed: heap=%u", (unsigned)xPortGetFreeHeapSize());
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
