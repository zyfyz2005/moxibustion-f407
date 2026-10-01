/**
 * @file if_store.h
 * @brief 配置存储服务（24C02，I2C1）。
 *
 * 存什么：只有两个字段——高温阈值、倒计时时长。计划书 v10 砍掉屏幕和 W25Q 之后，
 * 需要持久化的就剩这两项，所以 24C02 完全够用，不引外部 Flash。
 *
 * 硬件事实（已按原理图复核）：24C02 接在 **PB8 = I2C1_SCL / PB9 = I2C1_SDA**（I2C1，AF4），
 * A0/A1/A2 全接地 → 7 位从机地址 **0x50**（写 0xA0 / 读 0xA1），WP 悬空（可写）。
 * 注意别把它当成 PB6/PB7——芯片上 PB6/PB7 也带 I2C1 复用，但这块板子把它们给了摄像头 DCMI。
 *
 * 调用时机（重要，决定实现里能不能用 FreeRTOS API）：
 *   - if_store_load()  只在 main() 里、osKernelStart() **之前**调用。
 *                       实现内部**只允许**用 HAL 的轮询式 I2C 读写，不许碰任何 FreeRTOS API，
 *                       否则会在调度器启动前挂死。
 *   - if_store_save()  运行期由 ConfigTask 调用（唯一调用者）。
 */

#pragma once

#include "cm_types.h"

/** 本工程要持久化的全部配置 */
typedef struct {
    uint16_t th_max_c;     /**< 高温阈值，单位 ℃，范围 [APP_TH_MIN_C, APP_TH_MAX_C] */
    uint32_t duration_s;   /**< 倒计时时长，单位 s，范围 [APP_DUR_MIN_S, APP_DUR_MAX_S] */
} moxa_cfg_t;

/** 写 24C02 每写完一页回调一次（用于在长写过程中推进心跳） */
typedef void (*if_store_progress_fn)(void *user);

/**
 * @brief 读配置。读失败或 CRC 校验不过 → 用 defaults 填值并回写一次。
 * @param defaults 默认配置（阈值 40 ℃、时长 60 s 由 Application 的 app_cfg.h 提供）。
 *                 做成参数而不是写死在本层：Interface 不依赖 Application，
 *                 若把默认值抄一份在这里，以后改默认值就必然出现两处不一致。
 * @param cfg      输出配置
 * @note 只能在调度器启动前调用；内部不得使用任何 FreeRTOS API
 * @return CM_OK = 读到有效配置；CM_ERR_CRC / CM_ERR_IO = 用了默认值（上层照常继续）
 */
cm_err_t if_store_load(const moxa_cfg_t *defaults, moxa_cfg_t *cfg);

/**
 * @brief 写配置（CRC8 + 按 8 字节页边界拆包）
 * @param cfg          要写入的完整配置（两个字段都会写）
 * @param on_page_done 每写完一页回调一次，可为 NULL
 * @param user         透传给 on_page_done 的指针
 * @return CM_OK 成功；CM_ERR_PARAM 入参非法；CM_ERR_IO 器件没应答或写后校验不一致
 */
cm_err_t if_store_save(const moxa_cfg_t *cfg, if_store_progress_fn on_page_done, void *user);
