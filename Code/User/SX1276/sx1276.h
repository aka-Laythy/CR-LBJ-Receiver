#ifndef __SX1276_H
#define __SX1276_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  SX1276 精简接口（DIO Bit-Stream 方案）
 *  SPI 仅在初始化时使用；接收阶段完全由 DIO1/DIO2 驱动。
 * ================================================================ */

/* 操作结果码 */
typedef enum {
    SX1276_OK          = 0,
    SX1276_ERR_NO_CHIP = -1
} SX1276_Result_t;

/* 初始化并进入 FSK 连续接收模式（DIO1=DCLK, DIO2=DATA）
 * 返回 SX1276_OK / SX1276_ERR_NO_CHIP */
int8_t   SX1276_Init(void);

/* 芯片存在检测（Version 寄存器读 0x12 或 0x22） */
int8_t   SX1276_CheckChip(void);

/* 切换 LBJ 信道 (0/1/2) 并重新进入接收 */
void     SX1276_SetChannel(uint8_t ch);

/* 重新进入接收（若因错误切出模式可调用） */
void     SX1276_EnterRx(void);

/* 读取当前 RSSI（dBm，近似值） */
int16_t  SX1276_ReadRSSI(void);

/* 调试用寄存器访问 */
void     SX1276_WriteReg(uint8_t addr, uint8_t val);
uint8_t  SX1276_ReadReg(uint8_t addr);

/* 自检：回读关键寄存器，通过 BLE 输出结果 */
void     SX1276_SelfTest(uint8_t (*send)(const uint8_t *data, uint16_t len));

/* 从 W25Q64 加载/保存带宽配置 (RegRxBw / RegAfcBw) */
void     SX1276_LoadBwConfig(void);
void     SX1276_SaveBwConfig(void);

#ifdef __cplusplus
}
#endif

#endif
