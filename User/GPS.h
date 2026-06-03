#ifndef __GPS_H
#define __GPS_H

#include "debug.h"

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

void     GPS_Init(void);
uint8_t  GPS_SendByte(uint8_t byte);
uint8_t  GPS_SendData(const uint8_t *data, uint16_t len);
uint8_t  GPS_SendString(const uint8_t *str);
uint8_t  GPS_ReadByte(uint8_t *byte);
uint16_t GPS_ReadData(uint8_t *buf, uint16_t len);
uint16_t GPS_GetRxCount(void);
void     GPS_ClearRx(void);
uint8_t  GPS_IsTxBusy(void);

#endif /* __GPS_H */
