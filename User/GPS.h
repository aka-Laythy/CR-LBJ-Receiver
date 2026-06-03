#ifndef __GPS_H
#define __GPS_H

#include "debug.h"
#include <stdbool.h>

/*=============================================================================
 * GPS 模块驱动（DX-GP21 GNSS 定位模组）
 * 接口：USART1
 * 引脚：PC0(TX) / PC1(RX)  — PartialRemap3
 * 波特率：115200 bps（模组默认值），8N1，无流控
 * 通信方式：中断非阻塞式收发，基于环形缓冲区
 * 数据格式：NMEA-0183 协议
 *===========================================================================*/

#define GPS_RX_BUF_SIZE  512
#define GPS_TX_BUF_SIZE  128

/*=============================================================================
 * GPS 解析数据结构体
 *===========================================================================*/
typedef struct {
    double      latitude;      // 纬度，度格式（如 29.345612）
    char        lat_dir;       // 'N' 或 'S'
    double      longitude;     // 经度，度格式（如 104.712345）
    char        lon_dir;       // 'E' 或 'W'
    uint16_t    year;          // 年（如 2026）
    uint8_t     month;         // 月（1-12）
    uint8_t     day;           // 日（1-31）
    uint8_t     hour;          // 时（北京时间 0-23）
    uint8_t     minute;        // 分（0-59）
    uint8_t     second;        // 秒（0-59）
    uint32_t    timestamp;     // UTC 时间戳（秒）
    bool        valid;         // 定位是否有效
} GPS_Data_TypeDef;

/*=============================================================================
 * 函数声明
 *===========================================================================*/
void     GPS_Init(void);
uint8_t  GPS_SendByte(uint8_t byte);
uint8_t  GPS_SendData(const uint8_t *data, uint16_t len);
uint8_t  GPS_SendString(const uint8_t *str);
uint8_t  GPS_ReadByte(uint8_t *byte);
uint16_t GPS_ReadData(uint8_t *buf, uint16_t len);
uint16_t GPS_GetRxCount(void);
void     GPS_ClearRx(void);
uint8_t  GPS_IsTxBusy(void);

void     GPS_ParseNMEA(void);                          // 解析 NMEA 数据（从接收缓冲区读取并解析）
bool     GPS_GetLatestData(GPS_Data_TypeDef *data);    // 获取最新有效数据
void     GPS_ClearData(void);                          // 清除缓冲数据

#endif /* __GPS_H */
