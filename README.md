# Project-20261001-moxibustion-f407

基于 STM32F407ZGT6 + FreeRTOS 的艾灸控温控时报警装置（设备端重构，个人项目）。
分层沿用宠物定位器项目那套 **Core（CubeMX）/ Driver / Interface / Application / Common**，两个项目架构一致。

- 项目计划书 v11：`工作区/outputs/docs/Project-20261001-moxibustion-f407/20261001-STM32F407艾灸装置项目计划书.md`
- M1 配置清单：`docs/M1-CubeMX配置清单.md`
- 原理图信息核对清单（引脚事实的唯一依据）：`docs/20261001-原理图信息核对清单.md`

## 项目概览

给艾灸治疗做的一台**控温 / 控时 / 报警**装置固件：一块 STM32F407ZGT6 同时承担采集、控制、报警与联网，跑 FreeRTOS。

| 项 | 内容 |
|---|---|
| 硬件 | 普中-麒麟 STM32F407ZGT6 开发板；DHT11 温湿度（PG9）；无源蜂鸣器（PF8 / TIM13_CH1）；按键 PA0 / PE3 / PE4；LED PF9 / PF10；板载 ESP8266 模组（USART3，使能脚 PF6）；ST-Link 下载调试 |
| 软件 | FreeRTOS + ST HAL；自建四层代码（Common / Driver / Interface / Application），5 个应用任务 |
| 工具链 | Keil MDK5（ARMCC5 + MicroLIB）+ STM32F4xx_DFP；CubeMX 工程文件 `.ioc` 是配置的唯一入口 |
| 编译烧录 | 打开 `MDK-ARM/Project-20261001-moxibustion-f407.uvprojx` → Rebuild → `Options for Target → Debug` 确认用 ST-Link → Download（认准 Erase Done / Programming Done / Verify OK 三行） |
| 当前进度 | M1 骨架、M2 外设、M3 状态机 / 倒计时 / 报警 / 静音 / 复位均已上板验证；**M4 联网与 M5 看门狗未完成** |

> `Application/Inc/app_cfg.h` 里的 WiFi 账号与 MQTT device_id / 主题都是占位值，实际部署前必须整组替换。

## 一、分层与依赖方向（这套架构的核心）

```text
Application   业务：FreeRTOS 任务、状态机、协议报文
     │  只能往下调
     ▼
Interface     模块语义：温湿度服务 / 联网服务 / 存储服务 / 报警服务
     │
     ▼
Driver        器件驱动：DHT11 时序、蜂鸣器 PWM、串口 DMA、按键与 LED
     │
     ▼
Core / HAL    CubeMX 生成，只读

Common        公共工具，任何层都能用，它自己只依赖标准库
```

两条规则：

1. **上层可以调下层，下层绝不回头调上层。** 需要反向通知时用回调注册——`drv_uart` 收完一帧调用 Interface 注册进来的函数，而不是直接去动任务。
2. **Common 不依赖任何业务层。** 环形缓冲、CRC、JSON、日志都放这里，它们不该知道"艾灸""报警"是什么。

分层的实际收益（不是形式主义）：

- 换云平台（easyiothings → 自建 broker）：只改 `if_net` 和宏配置，5 个任务一行不动
- 调试口（USART1）和模组口（USART3）共用一套串口驱动：`drv_uart` 被复用两次，只是实例参数不同
- 以后换主控（F407 → 别的型号）：只有 Driver 层要重写，Application 一行不动

## 二、目录结构

```text
Project-20261001-moxibustion-f407/
├─ README.md                     本文件
├─ docs/                         设计文档与过程记录（实测数据、排查笔记）
│  ├─ 20261001-STM32F407艾灸装置项目计划书.md   v12 设计文档
│  ├─ M1-CubeMX配置清单.md
│  └─ 20261001-原理图信息核对清单.md
├─ Project-20261001-moxibustion-f407.ioc   CubeMX 工程文件，改配置的唯一入口
├─ Core/                         时钟/外设初始化/中断    ┐
├─ Drivers/                      官方 HAL + CMSIS        │ CubeMX 生成
├─ Middlewares/                  FreeRTOS 内核          │ 不手改
├─ MDK-ARM/                      Keil 工程 + 编译产物   ┘
├─ Driver/                       ← 我们：器件驱动
├─ Interface/                    ← 我们：模块接口
├─ Application/                  ← 我们：业务与任务
└─ Common/                       ← 我们：公共工具
```

每层内部都是 `Inc/` + `Src/` 两个子目录。

> **工程根为什么就在这一层**：CubeMX 生成到 `<位置>\<工程名>\`，所以位置要填**上一级**（`experiments\`）、
> 工程名填 `Project-20261001-moxibustion-f407`，文件才正好落在工程根、和我们四个层目录并排。
> 早期试过把位置填成 `firmware\`，结果多套了一层 `firmware\moxibustion-f407\`，Keil 里的相对路径全得变成
> `..\..\`。现在统一成：**工程根 = 本目录**，Keil 里的引用一律是 `..\Application\Inc` 这种一层 `..` 的形式。
> 打开 `.ioc` 重新生成时，CubeMX 以 `.ioc` 所在目录为工程根，所以不会再套一层。

> ⚠️ **注意 `Drivers/` 和 `Driver/` 只差一个 s**：前者是 CubeMX 生成的官方库（只读），后者是我们写的器件驱动。
> 现在这两个目录在工程根里**紧挨着**，改文件前先看清是哪一层。

## 三、各层文件清单（规划，按 M1→M4 逐步创建）

### Common/ —— 公共工具，与硬件无关

| 文件 | 职责 |
|---|---|
| `cm_types.h` | 基础类型、编译期断言、常用宏 |
| `cm_ringbuf.c/h` | 环形缓冲（AT 接收、日志缓冲用） |
| `cm_crc.c/h` | CRC8 / CRC16 |
| `cm_json.c/h` | 轻量 JSON 组装与字段解析（不引三方库） |
| `cm_time.c/h` | tick 换算与超时判断；另提供一个通用秒计数器原语，**实例由 ControlTask 持有**，用来维护 `uptime_s` |
| `cm_log.c/h` | 日志格式化与写缓冲。**写口与锁都由上层注册进来**（不依赖 HAL / FreeRTOS）——`dbg_uart_mutex` 是 Application 层的东西，由 `app_log_init()` 接到这里 |

### Driver/ —— 器件驱动，只回答"怎么把这个器件读通写通"

| 文件 | 对应硬件 |
|---|---|
| `drv_uart.c/h` | 串口 DMA + IDLE 收发、环形缓冲（USART1 调试、USART3 模组） |
| `drv_dht11.c/h` | DHT11 40 bit 单总线时序（PG9） |
| `drv_pwm.c/h` | 蜂鸣器 PWM 启停与频率设定（PF8 / **TIM13_CH1**，APB1 → 84 MHz） |
| `drv_gpio.c/h` | 按键电平（PA0/PE3/PE4）、LED（PF9/PF10）、模组使能（PF6） |

驱动层**不含任何业务判断**——不判断"温度是否超阈值"，只负责读出温度。

### Interface/ —— 模块语义，业务层只认这一层

| 文件 | 对外接口 | 本层内部实现（换掉不影响上层） |
|---|---|---|
| `if_sensor.c/h` | 读温湿度、失败计数 | DHT11 单总线（PG9） |
| `if_net.c/h` | 连接管理、`publish(topic, payload, retain)`、下行报文回调 | AT 指令（USART3）+ MQTT 会话 |
| `if_alarm.c/h` | `set(kind)` / `mute()` / `stop()` 报警语义 | 蜂鸣器 PWM（PF8）+ LED（PF9/PF10） |
| `if_input.c/h` | 按键按下沿（`poll`）、按下状态 | 三个按键脚（PA0/PE3/PE4）+ 30ms 消抖 |
| `if_log.c/h` | 把 `cm_log` 接到调试串口（接线用，不含格式化） | USART1 阻塞发送 + 上层给的互斥量 |

> `if_input` 是补进清单的：按键消抖按计划书写在 ControlTask，但"读引脚电平"属于 Driver，
> 中间缺一层会迫使 Application 直接调 Driver，破坏分层。报文语义（JSON 怎么拼/怎么解析）
> 留在 Application 的 `app_protocol.c`，`if_net` 只负责传输。
>
> `if_log` 是同一个道理：`cm_log` 在 Common 层，只能靠注册回调拿到写口和锁；
> 而"写口是哪个串口、锁是哪个互斥量"只有上层知道。这个接线动作如果写在 Application 里，
> Application 就得直接调 `drv_uart`——跨层。放在 Interface 正合适。

### 参数怎么从 Application 流到下层（不破坏分层的做法）

Interface 层**不包含** `app_cfg.h`（那是 Application 的东西），所以可调参数一律**从参数传进去**：

| 下层需要的参数 | 谁传、怎么传 |
|---|---|
| 按键消抖时间窗 | `if_input_init(APP_KEY_DEBOUNCE_MS)` |
| 报警频率与节奏 | `if_alarm_init(&cfg)`，cfg 由 AlarmTask 用 `APP_ALARM_*` 填 |
| MQTT/WiFi 连接参数 | `if_net_init(&cfg, &huart3)`，cfg 由 NetTask 用 `MQTT_*` / `WIFI_*` 填 |
| Driver 自己的缓冲尺寸 | 直接定义在 `Driver/Inc/drv_uart.h`（`DRV_UART_NET_*`），不在 app_cfg.h 里重复一份 |

### Application/ —— 业务与任务，FreeRTOS 的东西都在这里

| 文件 | 职责 |
|---|---|
| `app_cfg.h` | **所有可调参数集中在这**（周期、心跳期限、队列深度、阈值范围、超时） |
| `app_types.h` | `MoxaState_t` / `CmdMsg_t` / `AckMsg_t` / `ConfigReq_t` / `ConfigResp_t` / `AtTxItem_t` |
| `app_ctx.c/h` | 队列、事件组、互斥量句柄与 `EVT_STATE_*` 事件位；共享状态 `g_moxa_state` |
| `app_fsm.c/h` | 状态机（WAITING / COUNTDOWN / ALARM + muted 规则） |
| `app_protocol.c/h` | MQTT 主题与报文组装、下行指令解析 |
| `app_heartbeat.c/h` | 心跳推进与期限检查 |
| `app_tasks.c/h` | 5 个任务的创建与入口声明；优先级/栈深统一取自 `app_cfg.h` |
| `task_control.c` | ControlTask：按键、状态机、倒计时、ACK 生成、秒计数、堆打印 |
| `task_alarm.c` | AlarmTask |
| `task_sensor.c` | SensorTask |
| `task_net.c` | NetTask：驱动 Interface 层的 AT 状态机，管 `at_tx_queue` / `ack_queue`、组装时机与心跳（**AT 状态机本体不在这里**） |
| `task_dog.c` | DogTask |

> 任务、队列、事件组都定义在 Application 层——它们是业务通信手段，不是驱动。Driver 层允许用信号量做收发同步，但不含业务判断。

## 四、计划书模块 → 文件映射

| 计划书里的东西 | 落到哪 |
|---|---|
| 全部可调参数 | `Application/Inc/app_cfg.h` |
| 应用层结构体（`moxa_cfg_t` / `MoxaState_t` / `CmdMsg_t` / `AckMsg_t` / `AtTxItem_t` / `SensorSample_t`），**按名字列，不写数字**——写数字必然随着新增字段/结构体过时 | `Application/Inc/app_types.h` |
| 队列 / 事件组 / 互斥量句柄 | `Application/Src/app_ctx.c` |
| 状态机本体 | `Application/Src/app_fsm.c` |
| 5 个 FreeRTOS 任务（`task_control` / `task_alarm` / `task_sensor` / `task_net` / `task_dog`） | `Application/Src/task_*.c` |
| MQTT 报文与指令解析 | `Application/Src/app_protocol.c` |
| 心跳：各任务自己调 `app_hb_kick()`，DogTask 调 `app_hb_check_all()` 统一判定 | `Application/Src/app_heartbeat.c` + `task_dog.c` |
| DHT11 时序 | `Driver/Src/drv_dht11.c` |
| 蜂鸣器 PWM + LED | `Driver/Src/drv_pwm.c` + `drv_gpio.c` |
| 串口 DMA + IDLE + 环形缓冲 | `Driver/Src/drv_uart.c` + `Common/Src/cm_ringbuf.c` |
| AT 命令状态机、URC 解析——**实现在 Interface 层**；Application 的 `task_net.c` 只负责驱动它、管队列和心跳 | `Interface/Src/if_net.c` |

## 五、Keil 分组与头文件路径

| Group | 指向 |
|---|---|
| `Application` | `Application/Src/*.c` |
| `Interface` | `Interface/Src/*.c` |
| `Driver` | `Driver/Src/*.c` |
| `Common` | `Common/Src/*.c` |
| `Core` | `Core/Src/*.c`（CubeMX 生成） |
| `Drivers/STM32F4xx_HAL_Driver` | 官方 HAL（CubeMX 生成） |
| `Middlewares/FreeRTOS` | 实际路径 `Middlewares/Third_Party/FreeRTOS/Source`（含 `include`、`CMSIS_RTOS`、`portable/RVDS/ARM_CM4F`、`portable/MemMang/heap_4.c`） |
| `Startup` | 启动文件 |

**Include Paths** 必须包含：`Core/Inc`、`Application/Inc`、`Interface/Inc`、`Driver/Inc`、`Common/Inc`、`Drivers/STM32F4xx_HAL_Driver/Inc`、`Drivers/CMSIS/Device/ST/STM32F4xx/Include`、`Drivers/CMSIS/Include`、FreeRTOS 的 `Source/include`、`Source/CMSIS_RTOS`、`Source/portable/RVDS/ARM_CM4F`。

> **为什么 CMSIS_RTOS 这条不能删**（有人会以为和"用原生 API"矛盾）：CubeMX 生成的 `main.c` 调的是
> `osKernelInitialize()` / `osKernelStart()`，并且把 `Middlewares/.../CMSIS_RTOS/cmsis_os.c` 加进了 Keil 工程，
> 所以这个 include path 是**必需的**。规矩只约束"我们自己写的代码"：任务用原生 `xTaskCreate` 建、
> 队列/事件组用原生 API，不写 `osThreadNew` 那一套（但 CubeMX 默认生成的 `defaultTask` 是例外，见第六节）。

## 六、CubeMX 重新生成后必做（每次都要过一遍）

0. **先检查 Keil 的调试器与烧录算法**（2026-10-02 踩过）：CubeMX 重新生成会连 `.uvoptx` 一起重写，把调试器从你选的 **ST-Link** 换回默认的 **CMSIS-DAP**（`.uvoptx` 里 `<pMon>` 由 `STLink\ST-LINKIII-KEIL_SWO.dll` 变成 `BIN\CMSIS_AGDI.dll`）。插着 ST-Link 却让工程找 CMSIS-DAP，下载会失败——严重时擦除做了一半、程序没写进去，表现是**板子像死了一样、串口一个字都没有**。所以生成后第一件事：`Options for Target → Debug → Use:` 选回 **ST-Link Debugger**；`Utilities → Settings → Flash Download` 确认算法是 **STM32F4xx Flash**（0x08000000 / 1 MB）。下载时认准 Build Output 里的 `Erase Done / Programming Done / Verify OK` 三行。

1. **检查 Keil 里我们自己的四个分组（Application / Interface / Driver / Common）还在不在**。CubeMX 会重写 `.uvprojx`，自己加的分组和文件有可能被丢掉，丢了就重新 Add Existing Files（源文件本身不会丢）。
2. **逐项核对 `FreeRTOSConfig.h`**。它是 CubeMX 生成的会被覆盖——`configCHECK_FOR_STACK_OVERFLOW`、`configMAX_PRIORITIES`、`configTOTAL_HEAP_SIZE` 最容易被重置。
3. **确认 `stm32f4xx_hal_timebase_tim.c` 还在**，这是 HAL 时基跑在 TIM6 的证据；文件没了说明时基又被改回 SysTick。在 CubeMX 里对应 `SYS → Timebase Source = TIM6`（详见 `docs/M1-CubeMX配置清单.md` 第 3 节）。
4. **`defaultTask` 按现状保留**（2026-10-01 决定）：CubeMX 默认生成 `StartDefaultTask`（用 `osThreadCreate` 建，优先级映射后是 3、栈 128 word、每 1 ms 醒一次空转）。M1 清单原先写的"Tasks 页面不建任何任务"**已作废**——保留它，但要知道代价：约 0.5 KB 堆 + 每秒上千次无谓唤醒。原理由见 M1 清单第 6 节。
5. **`.ioc` 与 `FreeRTOSConfig.h` 必须纳入版本管理**，每次重新生成后先 `git diff` 看清改了什么再往下走——它们是"配置的唯一来源"和"被覆盖后最容易出问题"的两个文件。
6. **Keil 里勾 MicroLIB**（Options → Target → Use MicroLIB）。不勾时 `printf` 那套会带进约 6.7 KB Flash（其中浮点 3.5 KB）；本工程只打印整数，勾上直接省掉。

## 七、命名与注释规范

> 一句话原则：**看名字就知道它是哪一层的、干什么的**。
> 缩写只允许用公认技术缩写（UART / I2C / PWM / MQTT / JSON / CRC / EEPROM / DHT / AT），
> 自造缩写一律写全——`cnt`、`idx`、`hdl`、`proc` 这类省键盘的写法，读的人每次都要猜。

### 7.1 文件与目录

- 目录与文件一律小写蛇形；项目目录 `Project-YYYYMMDD-主题`
- 每个 `.c` 配同名 `.h`，头文件用 `#pragma once`；文件名前缀 = 它所属的层（见 7.2）
- Driver 层的粒度：**总线/外设级按外设命名**（`drv_uart`、`drv_pwm`、`drv_gpio`），
  **单一器件专用按型号命名**（`drv_dht11`）。
  不要写 `drv_i2c_ee` 这种"总线 + 器件缩写"混着来的名字——`ee` 不查不知道是 EEPROM，
  而且以后加第二个 I2C 器件时这个文件没地方安放

### 7.2 前缀表

| 对象 | 前缀 | 例子 |
|---|---|---|
| Common 层 | `cm_` | `cm_json`、`cm_ringbuf`、`cm_log` |
| Driver 层 | `drv_` | `drv_uart`、`drv_dht11`、`drv_pwm` |
| Interface 层 | `if_` | `if_net`、`if_alarm`、`if_input`、`if_log` |
| Application 公共 | `app_` | `app_cfg`、`app_fsm`、`app_heartbeat` |
| Application 任务 | `task_` | `task_control`、`task_net` |
| 设备业务语义（状态 / 指令 / 回执码） | `MOXA_` | `MOXA_ST_ALARM`、`MOXA_CMD_SET_TH`、`MOXA_CODE_BAD_PARAM` |
| 可调参数 | 与本模块一致 | `APP_HB_CTRL_MS`、`APP_Q_ACK_LEN`、`CM_LOG_BUF_SIZE` |
| 事件位 / 心跳 id / 待发队列种类 | `APP_` | `APP_EVT_STATE_ALARM`、`APP_HB_CTRL`、`APP_AT_TX_TELEMETRY` |

### 7.3 类型、函数、变量

- **类型**：
  - 结构体/联合体：**大驼峰 + `_t`**——`MoxaState_t`、`CmdMsg_t`、`AtTxItem_t`
    （沿用计划书里既定的写法，不改名）
  - 枚举类型：**小写蛇形 + `_e`**——`if_net_state_e`、`app_err_id_e`、`moxa_state_e`
    （和结构体一眼区分开：看到 `_e` 就知道是个枚举）
- **函数**：`<模块前缀>_<动词><名词>`，动词用完整词
  - 对外接口用模块前缀：`app_fsm_eval`、`if_net_mqtt_publish`
  - **文件内的 `static` 函数也要带前缀**，前缀取该文件的短名，避免"同一段代码里有的函数有前缀、有的没有"：

    | 文件 | static 函数前缀 | 例 |
    |---|---|---|
    | `task_control.c` | `ctrl_` | `ctrl_ack_emit` |
    | `task_net.c` | `net_` | `net_pump_tx` |
    | `app_ctx.c` | `ctx_` | `ctx_create_queues` |
    | `app_tasks.c` | `tasks_` | `tasks_create_one` |
    | `cm_json.c` | `json_` | `json_key_equals` |
    | `cm_ringbuf.c` | `ringbuf_` | `ringbuf_advance_tail` |

- **变量**：
  - 结构体成员、参数、局部变量一律小写蛇形，**名字要有实义**
  - 文件内的全局变量加 `s_` 前缀（`s_pending`、`s_heartbeat_last`）；
    写在 `.h` 里 `extern` 的跨文件变量不加 `s_`，用模块前缀（`q_sensor`、`eg_state`、`mtx_state`）
  - **禁止自造缩写与单字母**（循环下标 `i` / `j` / `k` 除外）：
    ✗ `st` `smp` `req` `it` `a` `w` `p` `v` `rb` `r` `n` `cnt` `idx` `nr`
    ✓ `state` `sample` `request` `item` `ack` `writer` `parsed` `value` `ringbuf` `result` `count` `index`
  - 带单位的量把单位写进名字：`timeout_ms`、`period_s`、`freq_hz`、`temp_c`
  - 布尔量要能读出真假：`valid`、`active`、`overflow`、`has_value` 可以；`flag`、`en`、`sta` 不行

### 7.4 宏与枚举值

- 宏和枚举值全大写 + 下划线，**必须带模块前缀**，数值类还要带单位：
  `APP_HB_SENSOR_MS`、`APP_ALARM_FREQ_TEMP_HZ`、`CM_LOG_BUF_SIZE`
- **一个概念只允许一种前缀**：心跳既出现在 `app_cfg.h`（`APP_HB_SENSOR_MS`）又出现在
  `app_heartbeat.h`（心跳 id），两处都用 `APP_HB_`，不允许一处 `APP_HB_`、一处 `HB_`
- **所有可调参数集中在 `Application/Inc/app_cfg.h`**，各 `.c` 里不写魔数

### 7.5 注释

- 语言：中文正文 + 英文技术名词；**统一用 `/* */`，不用 `//`**
- **文件头**（每个 `.c` / `.h` 都要有）：

  ```c
  /**
   * @file  cm_json.c
   * @brief 轻量 JSON 实现
   * @note  只支持扁平对象；数值只支持整数
   */
  ```

- **函数注释**：
  - `.h` 里的对外函数：`@brief` 必写；有返回值写 `@return`；有坑（时序紧、会阻塞、
    只能在调度器启动前调、线程约束）写 `@note`
  - `@param` 的取舍：**两个及以上参数必须逐个写**（含单位与取值范围）；只有一个参数、
    且参数名已经把意思说全的不强求（例如 `cm_ringbuf_count(rb)`）——写了是噪声，
    不写不算违规。规则的目的是让人读懂，不是把字段凑齐
  - `.c` 里的 `static` 函数：至少一行 `@brief`
- **段分隔**：同一文件里只用一种风格，统一写成
  `/*==================== 段标题 ====================*/`
- **行内注释**：写**为什么**，或者标出**不明显的意图**，不复述代码字面动作
  - ✗ `/* 把 head 加一 */`——代码已经写了
  - ✓ `/* head 只由生产者写、tail 只由消费者写，所以 16 位读写不用加临界区 */`
  - 允许简短的"进度标记"，如 `app_hb_kick(APP_HB_SENSOR, now);  /* 采集开始前 */`
- **数值一律带单位**（ms / s / Hz / ℃ / % / B / word），注释里的推导式子要写全，
  比如"最坏复位时间 = 2.5 s + 0.5 s + 4 s = 7.0 s"，不要只给结论数
- **待办**用 `@todo`，并写清"什么条件下要做"，不写"以后优化"这种空话

## 八、从零重建本工程（作者本机流程，可选）

1. CubeMX 新建工程：**位置选 `experiments\`、工程名填 `Project-20261001-moxibustion-f407`**，生成的文件会正好落在工程根（位置栏填的是上一级，CubeMX 会再拼一层工程名；详见"二、目录结构"里的说明）。已生成好的工程直接打开 `Project-20261001-moxibustion-f407.ioc` 改配置即可。
2. 按 `docs/M1-CubeMX配置清单.md` 配外设与 FreeRTOS，生成 Keil 工程
3. 先把 `Common/` 和 `Application/app_ctx.c` 写出来（不依赖硬件，可离线写完）
4. 再按 M1 出口条件验证：5 个任务心跳、栈溢出钩子、堆占用打印

## 第三方代码与许可

本工程**自写部分**（`Common/`、`Driver/`、`Interface/`、`Application/`，以及 `Core/` 下的初始化配置与 Keil 工程文件）采用 **MIT** 许可，见根目录 `LICENSE`。

仓库里还随工程分发了第三方代码——它们由 CubeMX 生成时一并落地，各自沿用原许可，许可文件都在原位：

| 位置 | 来源 | 许可 |
|---|---|---|
| `Drivers/STM32F4xx_HAL_Driver/` | STMicroelectronics STM32F4 HAL Driver | BSD-3-Clause（`LICENSE.txt`） |
| `Drivers/CMSIS/Device/ST/STM32F4xx/` | STMicroelectronics CMSIS 设备头文件 | BSD-3-Clause（`LICENSE.txt`） |
| `Drivers/CMSIS/Include/` | ARM CMSIS Core | Apache-2.0（`LICENSE.txt`） |
| `Middlewares/Third_Party/FreeRTOS/` | Amazon FreeRTOS 内核 | MIT（`Source/LICENSE`） |

> 除 `Core/Inc/FreeRTOSConfig.h` 这类工程自身的配置外，上述第三方文件内容未被修改，随仓库分发只为满足编译依赖。
