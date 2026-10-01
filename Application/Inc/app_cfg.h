/**
 * @file app_cfg.h
 * @brief 全工程可调参数集中地。要改行为先改这里，不要把魔数散到各个 .c 里。
 *
 * 数值出处：计划书 v10 第二节（任务表 / 队列）、第四节（业务）、
 *           第五节（联网与 MQTT）、第六节（心跳与看门狗）。
 * 凡是"计划书里有明确数字"的，这里不重新设计，只做落位。
 */

#pragma once

/*==============================================================
 * 一、任务：优先级 / 栈深（word，CM4 上 1 word = 4 B）
 * 优先级数字越大越高。表来自计划书第二节任务表。
 *==============================================================*/

#define APP_PRIO_DOG        7
#define APP_PRIO_CONTROL    5
#define APP_PRIO_ALARM      4
#define APP_PRIO_SENSOR     3
#define APP_PRIO_NET        2
#define APP_PRIO_CONFIG     1

#define APP_STACK_DOG       256
#define APP_STACK_CONTROL   512
#define APP_STACK_ALARM     256
#define APP_STACK_SENSOR    256
#define APP_STACK_NET       1024
#define APP_STACK_CONFIG    256

/* 任务名（最多 configMAX_TASK_NAME_LEN - 1 个字符） */
#define APP_TASK_NAME_DOG       "dog"
#define APP_TASK_NAME_CONTROL   "control"
#define APP_TASK_NAME_ALARM     "alarm"
#define APP_TASK_NAME_SENSOR    "sensor"
#define APP_TASK_NAME_NET       "net"
#define APP_TASK_NAME_CONFIG    "config"

/*==============================================================
 * 二、心跳与看门狗
 *
 * 心跳期限表（计划书第六节）：
 *   ControlTask  每轮 10ms 轮询末尾推进            期限 100 ms
 *   AlarmTask    每 50ms 调一次                    期限 500 ms
 *   SensorTask   采集开始前 + 结束后各一次          期限 2.5 s
 *   NetTask      AT 等待循环内每 200ms 推进一次      期限 2 s
 *   ConfigTask   空闲循环每 500ms、24C02 每页之间    期限 2 s
 *
 * 最坏复位时间推导（计划书第六节，改下面任何一个数都要重算并同步验收标准）：
 *   最坏复位时间 = 最大心跳期限 + DogTask 检查周期 + IWDG 超时
 *                = 2.5 s + 0.5 s + 4.0 s
 *                = 7.0 s        → 验收按 ≤8 s
 *==============================================================*/

#define APP_CTRL_PERIOD_MS          10u      /* ControlTask 轮询周期 */
#define APP_ALARM_TICK_MS           50u      /* AlarmTask 周期（也是报警节奏的推进粒度） */
#define APP_CONFIG_IDLE_MS          500u     /* ConfigTask 空闲等待 */

#define APP_HB_CTRL_MS              100u
#define APP_HB_ALARM_MS             500u
#define APP_HB_SENSOR_MS            2500u
#define APP_HB_NET_MS               2000u
#define APP_HB_CONFIG_MS            2000u

#define APP_DOG_PERIOD_MS           500u     /* DogTask 检查周期 */
#define APP_IWDG_TIMEOUT_MS         4000u    /* IWDG 超时（LSI≈32kHz，32 分频 → 4000 tick） */
#define APP_WORST_RESET_MS          7000u    /* = 2500 + 500 + 4000，仅作文档；验收值在下面 */
#define APP_ACCEPT_MAX_RESET_MS     8000u    /* 验收标准：≤8s 内必须复位 */

/**
 * IWDG 使能开关。
 * M1~M4 必须保持 0：调试期 IWDG 一旦启动就无法关闭，配合 DBG_IWDG_STOP 冻结才安全。
 * M5 做健壮性时才改成 1，并在 main.c 里注册喂狗回调。
 */
#define APP_IWDG_ENABLE             0

/*==============================================================
 * 三、业务参数
 *==============================================================*/

#define APP_TH_MIN_C                20u      /* 阈值下限 */
#define APP_TH_MAX_C                60u      /* 阈值上限 */
#define APP_DUR_MIN_S               1u       /* 时长下限 */
#define APP_DUR_MAX_S               3600u    /* 时长上限 */

/* 配置默认值：读 24C02 失败或 CRC 错时用，并由应用层作为参数传给 if_store_load()，
 * 保证"默认值"只有这一个出处（不写两份） */
#define APP_DEF_TH_C                40u
#define APP_DEF_DUR_S               60u

#define APP_KEY_DEBOUNCE_MS         30u      /* 按键消抖时间窗 */
#define APP_SENSOR_PERIOD_MS        2000u    /* DHT11 采集周期 */

/* 报警节奏与频率（M0 结论：无源蜂鸣器，必须 PWM 驱动） */
#define APP_ALARM_ON_MS             200u     /* 响 200ms */
#define APP_ALARM_OFF_MS            150u     /* 停 150ms */
#define APP_ALARM_FREQ_TEMP_HZ      2000u    /* 温度报警 2kHz */
#define APP_ALARM_FREQ_CD_HZ        3000u    /* 倒计时报警 3kHz */
#define APP_ALARM_MUTE_BLINK_MS     500u     /* 静音时 LED1 慢闪半周期 */

/* 配置写在途超时：ControlTask 发出请求后等这么久没收到匹配回执，就回 code=5 并清在途标记
 * （计划书第二节配置写路径第 7 步的 300ms） */
#define APP_CFG_RESP_TIMEOUT_MS     300u

/*==============================================================
 * 四、队列深度（计划书第二节"队列与满策略"）
 *   sensor      4   满：丢最旧再入队
 *   cmd         8   满：NetTask 0 超时，失败直接回 busy
 *   ack         8   满：丢队头最旧一条腾格子（绝不阻塞）
 *   cfg_req     1   同一时刻只允许一条配置请求在途
 *   cfg_resp    2   让"迟到响应与新响应重叠"结构性不可能
 *   at_tx       8   满：优先丢最旧 TELEMETRY
 *==============================================================*/

#define APP_Q_SENSOR_LEN            4u
#define APP_Q_CMD_LEN               8u
#define APP_Q_ACK_LEN               8u
#define APP_Q_CFG_REQ_LEN           1u
#define APP_Q_CFG_RESP_LEN          2u
#define APP_Q_AT_TX_LEN             8u

/*==============================================================
 * 五、缓冲尺寸（全部静态分配，运行期禁止 malloc）
 *==============================================================*/

#define APP_AT_RX_RING_SIZE         512u     /* USART3 接收环形缓冲 */
#define APP_AT_TX_RING_SIZE         512u     /* USART3 发送环形缓冲 */
#define APP_AT_RESP_MAX             256u     /* 一条 AT 响应的最大长度 */
#define APP_TELE_PAYLOAD_MAX        288u     /* 遥测 JSON 最大长度 */
#define APP_ACK_PAYLOAD_MAX         96u      /* ACK JSON 最大长度 */
#define APP_STATUS_PAYLOAD_MAX      64u      /* 状态 JSON 最大长度 */
#define APP_DOWNLINK_MAX            192u     /* 下行报文最大长度 */

/*==============================================================
 * 六、联网与 MQTT
 *
 * 换 broker 时必须整组改：地址、端口、device_id、上下行主题、client id、遗嘱载荷。
 * 只改一两个会出现"连上了但收不到/发不出去"这类最难查的问题。
 *
 * 上行主题里同时跑遥测、ACK 和在线状态；下行主题收指令。
 * （计划书没写 ACK 走哪个主题，本实现统一走 MQTT_TOPIC_UP，
 *   客户端按 type 字段区分 telemetry / ack / status。）
 *==============================================================*/

#define MQTT_BROKER                 "easyiothings.com"
#define MQTT_PORT                   1883u
#define DEVICE_ID                   100685u
#define MQTT_TOPIC_UP               "Data/100685"
#define MQTT_TOPIC_DOWN             "Sub/100685"
#define MQTT_CLIENT_ID              "f407_moxa_100685"
#define MQTT_LWT_PAYLOAD            "{\"type\":\"status\",\"online\":false}"

/* WiFi 账号密码硬编码，换网络要重编译（计划书第五节的取舍） */
#define WIFI_SSID                   "replace_with_your_ssid"
#define WIFI_PASSWORD               "replace_with_your_password"

#define APP_NET_SLICE_MS            200u     /* NetTask 主循环步长 */
#define APP_AT_DEADLINE_MS          2000u    /* 每条 AT 命令的响应期限 */
#define APP_TELEMETRY_PERIOD_MS     3000u    /* 遥测周期 */
#define APP_NET_RECONNECT_MS        2000u    /* 重连重试间隔 */
#define APP_MQTT_KEEPALIVE_S        60u      /* 仅写入模组配置，保活由模组负责 */

/*==============================================================
 * 七、自检与诊断
 *==============================================================*/

#define APP_HEAP_PRINT_PERIOD_MS    30000u   /* ControlTask 每 30s 打印堆占用 */
#define APP_STAT_PRINT_PERIOD_MS    60000u   /* 每 60s 打印 DHT11 失败统计 */

/**
 * 栈高水位打印开关。
 * 打开后 ControlTask 的 30s 诊断行会额外带上各任务的剩余栈（word）。
 * 需要 FreeRTOSConfig.h 里 INCLUDE_uxTaskGetStackHighWaterMark = 1（CubeMX 默认是 1）。
 * 这条是本次实现加的，计划书没要求——作用是给 M1 的"故意把栈设小看是否触发溢出钩子"提供依据。
 */
#define APP_STACK_MONITOR_ENABLE    1

/*==============================================================
 * 八、入网与发布相关的行为约定
 *==============================================================*/

/** 遥测里的固件版本串（上报给客户端做版本核对） */
#define APP_FW_VERSION              "f407-v1"

/** MQTT 发布失败不落盘，只累加 err_cnt，等下一个周期重发（计划书第五节） */
#define APP_PUBLISH_RETRY_BY_PERIOD 1
