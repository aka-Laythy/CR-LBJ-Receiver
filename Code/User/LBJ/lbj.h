#ifndef __LBJ_H
#define __LBJ_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  LBJ 消息结构（依据 TB/T 3504-2018 第 9.1.2 节）
 *
 *  基本帧 [RIC 1234000]: [5C 车次号][3C 速度][5C 公里标] = 13 BCD 字符
 *  扩展帧 [RIC 1234002]: 机车类型/编号、线路名等
 * ================================================================ */
#define LBJ_RIC_APPROACH  1234000UL
#define LBJ_RIC_CLOCK     1234008UL
#define LBJ_RIC_EXTENDED  1234002UL

#define LBJ_MAX_SESSIONS  4
#define LBJ_SESSION_TTL_MS 10000

typedef struct {
    uint32_t ric;               /* POCSAG 地址 */
    uint8_t  function;          /* 功能码: 1=奇数/下行, 3=偶数/上行 */

    char     raw_text[256];     /* 原始 BCD 解码字符串 */
    uint8_t  raw_len;

    /* 基本帧 */
    bool     has_train_id;
    char     train_id[8];       /* 列车号 (最多 5 位数字) */
    bool     train_odd_even;    /* false=奇数, true=偶数 */

    bool     has_speed;
    int16_t  speed;             /* 速度 km/h */

    bool     has_km;
    bool     km_negative;
    char     km_post[8];        /* 公里标字符串 (最多 5 位数字, 不含小数点) */

    /* 扩展帧 */
    bool     is_extended;       /* 是否已收到扩展帧 */
    char     prefix[4];         /* 机车字母前缀 (SS/DF/HX/CR ...) */
    uint16_t loco_type_code;    /* 机型代码 */
    char     loco_model[24];    /* 机车型号描述 (如 "HXD1") */
    char     loco_number[8];    /* 机车编号 (如 "1234") */
    uint8_t  route_gbk[32];     /* 线路名 GBK 字节 (用于 OLED 显示) */
    uint8_t  route_gbk_len;

    int16_t  rssi_dbm;          /* 接收信号强度 */
} LBJ_Message_t;

/* 回调原型 */
typedef void (*LBJ_Callback_t)(const LBJ_Message_t *msg);

void LBJ_Init(LBJ_Callback_t cb);
void LBJ_ParsePOCSAG(uint32_t ric, uint8_t function,
                      const char *text, uint8_t len,
                      int16_t rssi);

/* 获取最近一次完整解析的 LBJ 消息 (菜单显示用) */
const LBJ_Message_t * LBJ_GetLatest(void);

#ifdef __cplusplus
}
#endif

#endif /* __LBJ_H */
