# M1 工程骨架 · CubeMX + Keil 配置清单

> 配合 `outputs/docs/Project-20261001-moxibustion-f407/20261001-STM32F407艾灸装置项目计划书.md`（v11）使用。
> 本清单把计划书第二节的配置逐项落到 CubeMX 的界面上，照着点即可，不需要再查资料。
> 引脚已按原理图确认（M0 已通过，见第 0 节），本清单里的引脚值可直接照填。

## 0. M0 硬件闸门（已通过 · 2026-10-01）

对原理图逐脚确认完毕，正文所有引脚以此为准：

| 项 | 结论 |
|---|---|
| DHT11 / DS18B20 数据脚 | **PG9**（第 124 脚）。J6 座：1=VCC3.3 / 2=DQ / 3=NC / 4=GND，板载 R36 10K 上拉 |
| WiFi 模组 | **USART3**：PB10(USART3_TX)→**W_TX**（模组 URXD）、PB11(USART3_RX)←**W_RX**（模组 UTXD）；**CH_PD 使能脚 = PF6**（高电平工作）；模组状态灯 = PC0 |
| 蜂鸣器 | **无源**，接 **PF8**，S8050（Q2）+ R37 1K 驱动，必须 PWM 驱动 → **TIM13 要启用**（PF8 的定时器复用只有 TIM13_CH1） |

> 接线提醒：J6 是 4 针座，而 DHT11 是三针模块（VCC/DATA/GND），直接插会错位，要用杜邦线按 1↔VCC、2↔DATA、4↔GND 接。

仍待实测的一小项：WK_UP / KEY1 / KEY0 按下时的电平（原理图已给结论，上电复核一次即可）。
LED0/LED1 **共阳**（低电平点亮）已由原理图确认，不必再翻转试验。
另外板级引脚还有一项建议实物通断复核：PF8 是否真连到 R37（详见计划书第三节"待实物确认项"）。

## 1. 新建工程

- 芯片：**STM32F407ZGTx**（LQFP144）
- Toolchain：**MDK-ARM V5**
- 位置 + 工程名 → 生成到：`experiments\Project-20261001-moxibustion-f407\`（**工程根就是这一层**）

> **现状（2026-10-01 已生成，照现状核对即可）**：`.ioc` = `Project-20261001-moxibustion-f407.ioc`，
> 与 `Core\`、`Drivers\`、`Middlewares\`、`MDK-ARM\` 一起位于工程根；我们自建的四个层目录
> （Driver / Interface / Application / Common）**也在工程根，与 `Core\` 并排**，CubeMX 不会动它们。
>
> **位置栏怎么填**：CubeMX 生成到 `<位置>\<工程名>\`。所以位置填 `experiments\`、工程名填
> `Project-20261001-moxibustion-f407`，文件才正好落在工程根。早期把位置填成 `firmware\`，结果多套了
> 一层 `firmware\moxibustion-f407\`，Keil 里的相对路径全变成 `..\..\`——这是踩过的坑，别再来一次。
> 打开已有 `.ioc` 重新生成时，CubeMX 以 `.ioc` 所在目录为工程根，不会再套一层。

## 2. RCC 与时钟树

**Pinout & Configuration → System Core → RCC**

- High Speed Clock (HSE)：**Crystal/Ceramic Resonator**（PH0/PH1，板载 8 MHz）
- Low Speed Clock (LSE)：**Disable**（不用外部 32.768kHz 晶振，PC14/PC15 留空）

**Clock Configuration** 按下表填，最终 SYSCLK 落在 168 MHz：

| 项 | 值 | 说明 |
|---|---|---|
| PLL Source | HSE | 8 MHz |
| PLLM | 8 | 得到 1 MHz 的 PLL 输入 |
| PLLN | 336 | VCO = 336 MHz |
| PLLP | 2 | SYSCLK = 168 MHz |
| PLLQ | 7 | 48 MHz（虽然不用 USB，保持合法值） |
| AHB Prescaler | 1 | HCLK = 168 MHz |
| APB1 Prescaler | 4 | APB1 = 42 MHz |
| APB2 Prescaler | 2 | APB2 = 84 MHz |
| Flash Latency | 5 WS | 168 MHz @ 3.3V |

**关键推论（后面配 PWM 要用）**：

- APB2 预分频不是 1，所以 **APB2 上的定时器时钟 = 84 × 2 = 168 MHz**
- APB1 预分频不是 1，所以 **APB1 上的定时器时钟 = 42 × 2 = 84 MHz**
- TIM13 在 APB1 → 84 MHz；TIM6 在 APB1 → 84 MHz
- （TIM11 在 APB2 → 168 MHz，本项目**不用** TIM11：它的 CH1 在 PF7，而 PF7 在板上接光敏）

## 3. SYS 与时基（最容易忘、忘了全盘错）

**System Core → SYS**

- Debug：**Serial Wire**（保留 PA13/PA14，别的都关掉，否则占引脚）
- **Timebase Source：TIM6** ← 必须从默认的 SysTick 改过来

> 原因：FreeRTOS 会接管 SysTick 做任务调度，HAL 再依赖 SysTick 的话 `HAL_Delay` 和所有 HAL 超时参数全部失准。
> TIM6 是基本定时器，没有输出引脚，专门当时基最合适。CubeMX 会自动把它配成 1 ms 周期（PSC=83，ARR=999）。

## 4. 外设配置

### USART1 —— 调试串口（板载 CH340）

| 参数 | 值 |
|---|---|
| Mode | Asynchronous |
| 引脚 | PA9 = TX，PA10 = RX |
| Baud Rate | 115200 |
| Word / Parity / Stop | 8 / None / 1 |

无需 DMA，也不用中断（用阻塞式 `printf` 重定向即可）。若后续想让日志不占 CPU，再加 TX DMA。

### TIM13 —— 蜂鸣器 PWM（PF8，已确认无源，必须配）

| 参数 | 值 |
|---|---|
| Mode | PWM Generation CH1 |
| 引脚 | **PF8** |
| Counter Settings | Prescaler = **83**（计数频率 1 MHz） |
| Counter Period (ARR) | 499 → 输出 2 kHz |
| Pulse (CCR) | 250（50% 占空比） |

频率换算表（TIM13 在 **APB1 → 84 MHz**，PSC 固定 83 → 计数 1 MHz）：

> **为什么不是 167**：v11 之前按 TIM11 在 APB2（168 MHz）算的 PSC=167。改到 TIM13 后时钟变成 84 MHz，
> PSC 必须改成 83，ARR/CCR 不变（计数频率仍然是 1 MHz）。
> **在 CubeMX 里选 PF8 时只能选到 TIM13_CH1**——如果下拉框里出现 TIM11_CH1，那是选到 PF7 上了。

| 想要频率 | ARR | CCR（50%） |
|---|---:|---:|
| 温度报警 2 kHz | 499 | 250 |
| 倒计时报警 3 kHz | 332 | 166 |

切换报警类型时改 `ARR` 和 `CCR` 两个寄存器，不用重建定时器。

> 板载 Q2 是 S8050 NPN，蜂鸣器正极接 VCC3.3、负极接集电极，**高电平导通**，所以 PWM 输出高就是响。若日后换板换成有源蜂鸣器，则关掉 TIM13，PF8 改设 `GPIO_Output`，改用开关节奏区分两类报警。

### GPIO

| 引脚 | 网络名 | Mode | 上下拉 | 初始电平 |
|---|---|---|---|---|
| PA0 | WK_UP（开关机） | GPIO_Input | Pull-down | — |
| PE3 | KEY1（静音） | GPIO_Input | Pull-up | — |
| PE4 | **KEY0**（复位） | GPIO_Input | Pull-up | — |
| **PF6** | **W_KEY（ESP8266 CH_PD）** | GPIO_Output | No pull | **高（模组工作）** |
| PF8 | BEEP | **TIM13_CH1 PWM**（不另配 GPIO） | — | 关（不响） |
| PF9 | LED0 | GPIO_Output | No pull | 灭（**共阳，低电平点亮**） |
| PF10 | LED1 | GPIO_Output | No pull | 灭（**共阳，低电平点亮**） |
| **PG9** | DHT11 / DS18B20 DQ | GPIO_Output（开漏）+ 读时切输入 | 外部上拉（R36 已焊） | 高 |
| PC0 | W_LED（模组状态灯） | GPIO_Input | No pull | — |
| PF7 | LIGHT（光敏） | 不用 | — | — |
| PB10 / PB11 | USART3 TX / RX | 复用（见下节） | — | — |

> PG9 是单总线：写的时候切输出、读的时候切输入。CubeMX 里先按 `GPIO_Output` 配，驱动里直接改 `MODER` 寄存器换方向（比 `HAL_GPIO_DeInit/Init` 快，单总线时序紧）。
> 上拉已由板载 R36（10K）提供，三针模块自带的 4.7K 与之并联约 3.2K，对 DHT11 完全在可用范围，不用拆。
> **PF6 别忘**：CH_PD 拉不高，模组根本不工作，AT 一条都不会回——这颗脚漏配是排查起来最费时间的坑。
> **按键上下拉别配成 No pull**：KEY 区**没有外部上拉/下拉电阻**，PA0 的平时低电平、PE3/PE4 的平时高电平全靠 MCU 内部电阻。
> 配错的表现是按键"自己会响"或者按了没反应，且随环境干扰变来变去——排查时先回来确认这一栏。

### USART3 —— WiFi 模组（已确认）

| 参数 | 值 |
|---|---|
| Mode | Asynchronous |
| 引脚 | **PB10 = USART3_TX → W_TX**（发给模组，接模组 URXD），**PB11 = USART3_RX ← W_RX**（模组发来，接模组 UTXD） |
| Baud Rate | 115200 |
| Word / Parity / Stop | 8 / None / 1 |
| DMA Request RX | 添加，**Circular** 模式 |
| DMA Request TX | 添加，**Normal** 模式 |
| NVIC | 使能全局中断（用于 IDLE 空闲中断） |

> P10 是 **2×3 排针**（左列 6/4/2 = U3_TX / USART3_TX / W_TX，右列 5/3/1 = U3_RX / USART3_RX / W_RX）。
> 出厂默认 WiFi = 跳帽短接 **(4,2) 与 (3,1)**；切 RS232 = **(6,4) 与 (5,3)**。
> 必须留在 WiFi 一侧，被拨到 RS232 一侧串口就切走了；上板前对着**实物丝印**核对一次列序。
> M1 只要做到 DMA + 空闲中断能收到字节不丢即可；发 `AT` 验 `OK` 是 M4 的事。

## 5. NVIC 中断优先级

**System Core → NVIC**，优先级分组保持默认的 **4 位抢占 / 0 位子优先级**。

| 中断 | 抢占优先级 | 理由 |
|---|---:|---|
| DMA 相关 | 5 | 会调用 FreeRTOS API |
| WiFi USART 全局中断 | 5 | 同上 |
| USART1（调试） | 5（若开中断） | 同上 |
| 其它不调 API 的 | 6～15 | 数值越大优先级越低 |

**规则**：任何会调用 `xxxFromISR` 的中断，抢占优先级数值**必须 ≥ 5**。数值小于 5 的中断不许调 FreeRTOS API，否则表现为随机死机。

CubeMX 生成的 `HAL_Init()` 会把 SysTick 优先级设成 15（最低），`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5` 正好与之相容。

## 6. FreeRTOS 中间件

**Middleware → FREERTOS**

- Interface：**CMSIS_V1**（CubeMX 只为生成内核源码和 `FreeRTOSConfig.h`）
- **我们自己的任务一律用原生 `xTaskCreate` 建**（见 `Application/Src/app_tasks.c`），不写 `osThreadNew` 那套包装，理由见计划书第二节。
- **Tasks 页面里那个 `defaultTask` 按现状保留**（2026-10-01 决定，此前"不建任何任务"的说法作废）。
  它是 CubeMX 的默认产物：`osThreadCreate` 建、优先级映射后是 3、栈 128 word、主体是 `for(;;) osDelay(1);`。
  保留的代价是约 0.5 KB 堆 + 每秒上千次无谓唤醒；好处是不用每次重新生成后再去对付它。
  注意 CubeMX 生成的 `main.c` 自己会调 `osKernelInitialize()` / `osKernelStart()`，`cmsis_os.c` 也在 Keil 工程里，
  所以 `Source/CMSIS_RTOS` 这条 Include Path **不能删**（README 第五节有说明）。

| 配置项 | 值 |
|---|---|
| `configTICK_RATE_HZ` | 1000 |
| `configMAX_PRIORITIES` | 8 |
| `configMINIMAL_STACK_SIZE` | 128（仅空闲任务用） |
| Memory Allocation | **heap_4** |
| `configTOTAL_HEAP_SIZE` | 32768 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 |
| `configUSE_MUTEXES` | Enabled |
| `configUSE_COUNTING_SEMAPHORES` | Enabled |
| `configUSE_TASK_NOTIFICATIONS` | Enabled |
| `configUSE_IDLE_HOOK` | Disabled |
| `configUSE_TICK_HOOK` | Disabled |
| `configUSE_PREEMPTION` | Enabled |

> 注意：CubeMX 里的 Use Mutexes、Use Event Groups 等开关会写进 `FreeRTOSConfig.h`。事件组没有单独开关，只要内核版本对就能用 `xEventGroupCreate`。

## 7. Project Manager

- Toolchain / IDE：**MDK-ARM V5**
- Code Generator：
  - 勾 **Generate peripheral initialization as a pair of .c/.h files per peripheral**（外设初始化分离，后面加模块不打架）
  - **Copy only the necessary library files**
  - 不勾 "Delete previously generated files"
- Advanced Settings：保持各外设独立 Init

## 8. 生成后必须手工做的五件事

0. **先把 Keil 的调试器选回来**：CubeMX 重生成会把 `.uvoptx` 里的调试器重置成 CMSIS-DAP（`BIN\CMSIS_AGDI.dll`），而你板子上是 ST-Link。不改的话下载会失败，甚至擦了没写进去——表现为板子像死了、串口毫无输出。`Options for Target → Debug → Use:` 选 **ST-Link Debugger**，`Utilities → Settings → Flash Download` 确认算法是 **STM32F4xx Flash**（1 MB）。下载成功的标志是 Build Output 里 `Erase Done / Programming Done / Verify OK`。

1. **确认 HAL 时基是 TIM6**：看 `main.c` 里 `HAL_InitTick` 的调用和 `stm32f4xx_hal_timebase_tim.c` 是否存在。缺文件说明 Timebase Source 没改。
2. **核对 `FreeRTOSConfig.h`**：第 6 节的六项参数逐个对照，CubeMX 偶尔会把 `configCHECK_FOR_STACK_OVERFLOW` 重置成 0。
3. **启动顺序写死在 `main()` 里，照抄下面这段**（顺序错了会在调度器起来之前挂死）：

   ```c
   moxa_cfg_t cfg;

  /* ① 配置：删掉 24C02 持久化后直接用 app_cfg.h 的默认值 */
  static const moxa_cfg_t cfg = { APP_DEF_TH_C, APP_DEF_DUR_S };
  app_ctx_init(&cfg);

  /* ② 接日志写口 */
  app_log_init();

  /* ③ 建 5 个任务（优先级/栈深取自 app_cfg.h） */
  app_tasks_create();

  /* ④ 开机打印复位原因：读 RCC->CSR 的 IWDGRSTF / PINRSTF 等标志 */

   osKernelStart();
   ```

4. **任务创建由我们自己的 `app_tasks_create()` 负责**（`Application/Src/app_tasks.c`）：`MX_FREERTOS_Init()` 里只调它一次，6 个任务的优先级 / 栈深集中写在 `Application/Inc/app_cfg.h`，不要在 `main.c` 里再抄一份数字。
5. **`app_log_init()` 在 `osKernelStart()` 之前调用**，它把 `cm_log` 的写口接到调试串口 USART1。不调用的话所有日志静默丢弃（纯逻辑代码靠这个特性才能在 PC 上链接运行）。
6. **开机打印复位原因**：读 `RCC->CSR` 的 `IWDGRSTF` / `PINRSTF` 等标志，看门狗复位要能一眼认出来。

> **Keil 里记得勾 MicroLIB**（Options → Target → Use MicroLIB）。链接自检显示：不用 MicroLIB 时，
> `printf/snprintf` 那套机制会带进约 6.7 KB Flash，其中浮点支持占 3.5 KB；本工程只打印整数，
> 勾上 MicroLIB 可以直接省掉这几 KB。勾了之后 `%f` 不可用——本工程本来也不用（没有浮点字段）。

## 9. M1 出口自检

全部满足才算 M1 完成：

- [ ] 调试串口 115200 能打印，中文不乱码
- [ ] 5 个任务都在跑：每 30s 的 `stack free(w)` 行里能看到 5 个任务（dog / control / alarm / sensor / net），且全程没有 `hb: TIMEOUT` 行
- [ ] 故意把某任务栈设成 128 word，能触发 `vApplicationStackOverflowHook`（验证栈检查真的开了）
- [ ] ControlTask 每 30s 打印一行 `xPortGetFreeHeapSize` 与 `xPortGetMinimumEverFreeHeapSize`
- [ ] 连续运行 10 分钟，最小剩余堆 ≥ 8 KB
- [ ] 上电复位原因是 `PINRSTF`（不是看门狗复位）

### 待硬件与已到位（2026-10-02 记）

- [x] **DHT11 已到位并读数正常**：13:07 的日志里 `diag` 行显示 `temp=29`（阈值 40，不触发报警，正确）。
  还差验收里"连续 500 个采集周期、总失败率 ≤1%"那一条：跑够时间后看 `dht11: total/fail/rate/max_consec` 那行，`fail` 应接近 0。
  注意：这版驱动只实现 DHT11 的单总线时序，**不支持 DS18B20**（1-Wire，时序完全不同）。
- [ ] **ESP8266 模组**：日志里 `net: RESET rx=0 B drop=0` 说明模组口一个字节都没收到，
  需要确认模组是否插在 P3 座上、P10 跳帽是否在 WiFi 侧（竖插下排两孔）。M1 不要求它有数据，M4 才用得上。

## 附：本阶段不要做的事

- **不要启动 IWDG**（M5 才做）。调试期 IWDG 一旦启动无法关闭，会被反复复位。
- **不要写 AT 命令**（M4 才做）。M1 只要能把 WiFi USART 的 DMA + IDLE 接收跑通、收到一堆字节不丢就够了。
