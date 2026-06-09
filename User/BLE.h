#ifndef __BLE_H
#define __BLE_H

#include "debug.h"

/*=============================================================================
 * BLE 模块驱动（DX-BT311 蓝牙模组）
 * 接口：USART2
 * 引脚：PD2(TX) / PD3(RX)  — PartialRemap3
 * 波特率：921600 bps，8N1，无流控
 * 控制脚：PC5  — 模组 KEY/RST 控制（当前输出低电平）
 * 通信方式：中断非阻塞式收发，基于环形缓冲区
 *===========================================================================*/

#define BLE_RX_BUF_SIZE  256
#define BLE_TX_BUF_SIZE  256

void     BLE_Init(void);
uint8_t  BLE_SendByte(uint8_t byte);
uint8_t  BLE_SendData(const uint8_t *data, uint16_t len);
uint8_t  BLE_SendString(const uint8_t *str);
uint8_t  BLE_ReadByte(uint8_t *byte);
uint16_t BLE_ReadData(uint8_t *buf, uint16_t len);
uint16_t BLE_GetRxCount(void);
void     BLE_ClearRx(void);
uint8_t  BLE_IsTxBusy(void);

#endif /* __BLE_H */
