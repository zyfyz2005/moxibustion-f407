/**
 * @file if_store.c
 * @brief 配置存储实现：16 字节配置块 + CRC8 + 按页拆包。
 *
 * 配置块布局（正好 16 字节 = 24C02 的 2 个页，所以每写一次都会走到页边界拆分那一步）：
 *
 *   偏移  长度  内容
 *   0     2     magic：'M' 'X'（用来判断这块到底写过没有）
 *   2     1     版本号（以后结构变了靠它兼容）
 *   3     1     有效载荷长度（= 6）
 *   4     2     高温阈值 th_max_c（小端，单位 ℃）
 *   6     4     倒计时时长 duration_s（小端，单位 s）
 *   10    4     保留（写 0）
 *   14    1     CRC8（覆盖第 0..13 字节）
 *   15    1     保留（写 0）
 *
 * 为什么 magic + 版本 + CRC 三样都要：magic 判断"空片"，版本防将来改结构读错，
 * CRC 防掉电写坏——三者缺一都会把"读到垃圾"当成"读到合法配置"。
 */

#include "if_store.h"

#include "drv_24c02.h"
#include "cm_crc.h"

#define IF_STORE_ADDR        0x00u   /**< 配置块起始地址 */
#define IF_STORE_BLOCK_SIZE  16u     /**< 配置块长度，单位字节 */
#define IF_STORE_PAYLOAD_LEN 6u      /**< 有效载荷长度：阈值 2 + 时长 4 */
#define IF_STORE_MAGIC0      0x4Du   /**< 'M' */
#define IF_STORE_MAGIC1      0x58u   /**< 'X' */
#define IF_STORE_VERSION     0x01u
#define IF_STORE_CRC_OFFSET  14u     /**< CRC 所在偏移 */
#define IF_STORE_CRC_LEN     14u     /**< CRC 覆盖的字节数（0..13） */

/* 合理性边界：只用来判"读到的是不是垃圾"。
 * 业务范围（阈值 20~60 ℃、时长 1~3600 s）由 ControlTask 在下行指令那一层把关，
 * 不在这里判——否则 Interface 就得去依赖 Application 的 app_cfg.h。 */
#define IF_STORE_TH_SANE_MIN_C    1u        /**< 低于 1 ℃ 只可能是数据坏了 */
#define IF_STORE_TH_SANE_MAX_C    100u
#define IF_STORE_DUR_SANE_MIN_S   1u
#define IF_STORE_DUR_SANE_MAX_S   86400u    /**< 一天；比这还大必然是垃圾 */

/** 把配置打包成 16 字节块 */
static void store_pack(const moxa_cfg_t *cfg, uint8_t *block)
{
    for (uint8_t i = 0u; i < IF_STORE_BLOCK_SIZE; i++) {
        block[i] = 0u;
    }

    block[0] = IF_STORE_MAGIC0;
    block[1] = IF_STORE_MAGIC1;
    block[2] = IF_STORE_VERSION;
    block[3] = IF_STORE_PAYLOAD_LEN;
    cm_put_u16_le(&block[4], cfg->th_max_c);
    cm_put_u32_le(&block[6], cfg->duration_s);
    /* 10..13 保留为 0，14 处放 CRC，15 保留为 0 */
    block[IF_STORE_CRC_OFFSET] = cm_crc8(block, IF_STORE_CRC_LEN);
}

/** 校验并解包；不合法返回 false */
static bool store_unpack(const uint8_t *block, moxa_cfg_t *cfg)
{
    uint16_t th_max_c;
    uint32_t duration_s;

    if ((block[0] != IF_STORE_MAGIC0) || (block[1] != IF_STORE_MAGIC1)) {
        return false;
    }
    if ((block[2] != IF_STORE_VERSION) || (block[3] != IF_STORE_PAYLOAD_LEN)) {
        return false;
    }
    if (cm_crc8(block, IF_STORE_CRC_LEN) != block[IF_STORE_CRC_OFFSET]) {
        return false;
    }

    th_max_c   = cm_get_u16_le(&block[4]);
    duration_s = cm_get_u32_le(&block[6]);
    if ((th_max_c < IF_STORE_TH_SANE_MIN_C) || (th_max_c > IF_STORE_TH_SANE_MAX_C)) {
        return false;
    }
    if ((duration_s < IF_STORE_DUR_SANE_MIN_S) || (duration_s > IF_STORE_DUR_SANE_MAX_S)) {
        return false;
    }

    cfg->th_max_c   = th_max_c;
    cfg->duration_s = duration_s;
    return true;
}

cm_err_t if_store_load(const moxa_cfg_t *defaults, moxa_cfg_t *cfg)
{
    uint8_t block[IF_STORE_BLOCK_SIZE];

    if ((defaults == NULL) || (cfg == NULL)) {
        return CM_ERR_PARAM;
    }

    if (drv_24c02_read(IF_STORE_ADDR, block, IF_STORE_BLOCK_SIZE) != CM_OK) {
        *cfg = *defaults;
        /* 读都读不到就别急着回写——总线可能还没就绪，硬写只会再失败一次 */
        return CM_ERR_IO;
    }

    if (!store_unpack(block, cfg)) {
        *cfg = *defaults;
        /* 空片或数据坏了：回写一次默认值，让下一次上电有个合法块 */
        (void)if_store_save(cfg, NULL, NULL);
        return CM_ERR_CRC;
    }

    return CM_OK;
}

cm_err_t if_store_save(const moxa_cfg_t *cfg, if_store_progress_fn on_page_done, void *user)
{
    uint8_t  block[IF_STORE_BLOCK_SIZE];
    uint8_t  verify[IF_STORE_BLOCK_SIZE];
    cm_err_t result;

    if (cfg == NULL) {
        return CM_ERR_PARAM;
    }

    store_pack(cfg, block);

    result = drv_24c02_write(IF_STORE_ADDR, block, IF_STORE_BLOCK_SIZE, on_page_done, user);
    if (result != CM_OK) {
        return result;
    }

    /* 写后读回校验：I2C 写丢字节不会报错，只有读回来比一次才靠得住 */
    if (drv_24c02_read(IF_STORE_ADDR, verify, IF_STORE_BLOCK_SIZE) != CM_OK) {
        return CM_ERR_IO;
    }
    for (uint8_t i = 0u; i < IF_STORE_BLOCK_SIZE; i++) {
        if (verify[i] != block[i]) {
            return CM_ERR_IO;
        }
    }
    return CM_OK;
}
