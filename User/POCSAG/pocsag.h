#ifndef __POCSAG_H
#define __POCSAG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  POCSAG 解码器配置
 * ================================================================ */
#define POCSAG_RAW_BUF_SIZE     512     /* 原始数据环形缓冲区大小 */
#define POCSAG_MAX_MSG_LEN      256     /* 单条消息最大 numeric 字符数 */
#define POCSAG_MSG_TIMEOUT_MS   3000    /* 消息结束超时 (ms) */

/* 调试开关: 如果收到的字符全是乱码，尝试注释掉下面这行。
 * TB/T 3504 规定 char 内 bit 为 LSB-first，但实机可能需要反转。
 * 注释掉后 nibble 不再反转，恢复为标准 MSB-first。 */
#define POCSAG_NIBBLE_REVERSE

/* ================================================================
 *  解码结果回调原型
 *  ric      : 21 位地址码
 *  function : 2 位功能码 (0..3)
 *  text     : numeric 解码后的字符串 (以 '\0' 结尾)
 *  len      : 字符串长度
 *  rssi     : 接收时的 RSSI (dBm, 近似值)
 * ================================================================ */
typedef void (*POCSAG_Callback_t)(uint32_t ric, uint8_t function,
                                   const char *text, uint8_t len,
                                   int16_t rssi);

/* ================================================================
 *  对外 API
 * ================================================================ */
void POCSAG_Init(POCSAG_Callback_t callback);
void POCSAG_FeedByte(uint8_t byte);
void POCSAG_FeedBytes(const uint8_t *data, uint8_t len);
void POCSAG_Process(void);
void POCSAG_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __POCSAG_H */
