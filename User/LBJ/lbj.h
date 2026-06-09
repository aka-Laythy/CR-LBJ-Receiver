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
 *  POCSAG 消息内容 = [5C 车次号][3C 速度][5C 公里标]
 *  共 13 个 BCD 字符，分为 3 个 POCSAG 消息码字。
 * ================================================================ */
typedef struct {
    uint32_t ric;               /* POCSAG 地址 */
    uint8_t  function;          /* 功能码: 1=奇数车次, 3=偶数车次 */

    char     raw_text[256];     /* 原始 BCD 解码字符串 (13 chars) */
    uint8_t  raw_len;

    bool     has_train_id;      /* 车次号是否有效 */
    char     train_id[8];       /* 列车号 (最多 5 位数字) */
    bool     train_odd_even;    /* 按功能位: false=奇数, true=偶数 */

    bool     has_speed;         /* 速度是否有效 */
    int16_t  speed;             /* 速度 km/h */

    bool     has_km;            /* 公里标是否有效 */
    bool     km_negative;       /* 负公里标 */
    char     km_post[8];        /* 公里标字符串 (最多 5 位数字) */

    int16_t  rssi_dbm;          /* 接收信号强度 */
} LBJ_Message_t;

/* 回调原型 */
typedef void (*LBJ_Callback_t)(const LBJ_Message_t *msg);

void LBJ_Init(LBJ_Callback_t cb);
void LBJ_ParsePOCSAG(uint32_t ric, uint8_t function,
                      const char *text, uint8_t len,
                      int16_t rssi);

#ifdef __cplusplus
}
#endif

#endif /* __LBJ_H */
