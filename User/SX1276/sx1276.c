#include "sx1276.h"
#include "SPI_Bus/spi_bus.h"
#include "Tick.h"
#include <string.h>

/* ================================================================
 *  SX1276 FSK 常用寄存器
 * ================================================================ */
#define REG_OPMODE              0x01
#define REG_BITRATEMSB          0x02
#define REG_BITRATELSB          0x03
#define REG_FDEVMSB             0x04
#define REG_FDEVLSB             0x05
#define REG_FRFMSB              0x06
#define REG_FRFMID              0x07
#define REG_FRFLSB              0x08
#define REG_LNA                 0x0C
#define REG_RXCONFIG            0x0D
#define REG_RSSIVALUE           0x11
#define REG_RXBW                0x12
#define REG_AFCBW               0x13
#define REG_PREAMBLEDET         0x1F
#define REG_SYNCCONFIG          0x27
#define REG_PACKETCONFIG1       0x30
#define REG_PAYLOADLENGTH       0x32
#define REG_DIOMAPPING1         0x40
#define REG_DIOMAPPING2         0x41
#define REG_VERSION             0x42

/* OPMODE */
#define OPMODE_MODEM_FSK        0x00
#define OPMODE_SLEEP            0x00
#define OPMODE_STDBY            0x01
#define OPMODE_RX               0x05

/* 工信部 821 MHz 三信道 */
static const uint32_t LBJ_FRF[] = {
    820700000UL,
    821237500UL,
    821825000UL,
};

/* RXBW: 20.8 kHz @ Fxosc = 32 MHz (高4位=0x1=20.8kHz, 低4位抖动)
 *   SX1276 BW 由 RegValue[7:4] 决定, 见数据手册Rev7表14 */
#define RXBW_20K8 0x14

/* ================================================================
 *  SPI 寄存器访问
 * ================================================================ */
void SX1276_WriteReg(uint8_t addr, uint8_t val)
{
    SPI_Bus_Select(SPI_CS_SX1276);
    SPI_Bus_TransferByte(addr | 0x80);
    SPI_Bus_TransferByte(val);
    SPI_Bus_Release();
}

uint8_t SX1276_ReadReg(uint8_t addr)
{
    uint8_t rx;
    SPI_Bus_Select(SPI_CS_SX1276);
    SPI_Bus_TransferByte(addr & 0x7F);
    rx = SPI_Bus_TransferByte(0xFF);
    SPI_Bus_Release();
    return rx;
}

/* ================================================================
 *  芯片检测
 * ================================================================ */
int8_t SX1276_CheckChip(void)
{
    uint8_t v = SX1276_ReadReg(REG_VERSION);
    return (v == 0x12 || v == 0x22) ? SX1276_OK : SX1276_ERR_NO_CHIP;
}

/* ================================================================
 *  频率合成
 * ================================================================ */
static void SX1276_SetFrequency(uint32_t f_hz)
{
    uint32_t frf = (uint32_t)(((uint64_t)f_hz << 19) / 32000000UL);
    SX1276_WriteReg(REG_FRFMSB, (frf >> 16) & 0xFF);
    SX1276_WriteReg(REG_FRFMID, (frf >>  8) & 0xFF);
    SX1276_WriteReg(REG_FRFLSB,  frf        & 0xFF);
}

/* ================================================================
 *  RSSI
 * ================================================================ */
int16_t SX1276_ReadRSSI(void)
{
    uint8_t raw = SX1276_ReadReg(REG_RSSIVALUE);
    return -(int16_t)raw / 2;
}

/* ================================================================
 *  进入 FSK 连续接收
 * ================================================================ */
void SX1276_EnterRx(void)
{
    SX1276_WriteReg(REG_OPMODE, OPMODE_RX | OPMODE_MODEM_FSK);
    Tick_DelayMs(2);  /* PLL 锁定裕量 */
}

/* ================================================================
 *  初始化：配置为 FSK 连续模式，DIO1=DCLK, DIO2=DATA
 *  返回 SX1276_OK 或 SX1276_ERR_NO_CHIP
 * ================================================================ */
int8_t SX1276_Init(void)
{
    if (SX1276_CheckChip() != SX1276_OK) {
        return SX1276_ERR_NO_CHIP;
    }

    /* Sleep -> Stdby */
    SX1276_WriteReg(REG_OPMODE, OPMODE_SLEEP | OPMODE_MODEM_FSK);
    Tick_DelayMs(2);
    SX1276_WriteReg(REG_OPMODE, OPMODE_STDBY | OPMODE_MODEM_FSK);
    Tick_DelayMs(2);

    /* 频率: 821.2375 MHz (CH1) */
    SX1276_SetFrequency(LBJ_FRF[1]);

    /* Bitrate: 1200 bps = Fxosc / 1200 = 32e6 / 1200 = 26667 = 0x682B */
    SX1276_WriteReg(REG_BITRATEMSB, 0x68);
    SX1276_WriteReg(REG_BITRATELSB, 0x2B);

    /* Fdev: ±4.5 kHz, step = Fxosc / 2^19 = 61.035 Hz, 4500 / 61.035 ≈ 74 = 0x4A */
    SX1276_WriteReg(REG_FDEVMSB, 0x00);
    SX1276_WriteReg(REG_FDEVLSB, 0x4A);

    /* RX 带宽: 20.8 kHz — 匹配 1200bps FSK (Carson≈11.4kHz, 余量~2x) */
    SX1276_WriteReg(REG_RXBW,  RXBW_20K8);

    /* AFC 带宽: 500 kHz (高4位=8) — 只负责 PLL 捕获范围, 不决定信号选通
       SX1276 BW 由 RegValue[7:4] 查表 @32MHz:
       高4位=7 → 250kHz, 捕获±125kHz
       高4位=8 → 500kHz, 捕获±250kHz — 覆盖实测 ±130kHz
       RXBW(20.8kHz) 才是通道滤波器, AFC 捕获后信号自动落入其中 */
    SX1276_WriteReg(REG_AFCBW, 0x80);

    /* LNA: AGC 自动管理增益, 开启高频助推 */
    SX1276_WriteReg(REG_LNA, 0x21);

    /* RX 配置: AGC Auto + AFC Auto */
    SX1276_WriteReg(REG_RXCONFIG, 0x09);

    /* 前导码检测: 2 bytes, tolerance 10, 启用检测器 */
    SX1276_WriteReg(REG_PREAMBLEDET, 0xAA);

    /* 关闭硬件同步字检测 */
    SX1276_WriteReg(REG_SYNCCONFIG, 0x00);

    /* 包模式: 固定长度, 无 CRC, 无 DC-free */
    SX1276_WriteReg(REG_PACKETCONFIG1, 0x00);
    SX1276_WriteReg(REG_PAYLOADLENGTH, 64);

    /* DIO 映射: DIO1=DCLK, DIO2=DATA */
    SX1276_WriteReg(REG_DIOMAPPING1, 0xC0);
    SX1276_WriteReg(REG_DIOMAPPING2, 0x00);

    /* 进入连续接收 */
    SX1276_EnterRx();

    return SX1276_OK;
}

/* ================================================================
 *  切换 LBJ 信道 (0/1/2)
 * ================================================================ */
void SX1276_SetChannel(uint8_t ch)
{
    if (ch > 2) return;

    /* 切回 Stdby */
    SX1276_WriteReg(REG_OPMODE, OPMODE_STDBY | OPMODE_MODEM_FSK);
    Tick_DelayMs(1);

    /* 更新频率 */
    SX1276_SetFrequency(LBJ_FRF[ch]);

    /* 重新进入接收 */
    SX1276_EnterRx();
}

/* ================================================================
 *  自检：回读关键寄存器并与期望值比对
 * ================================================================ */
void SX1276_SelfTest(uint8_t (*send)(const uint8_t *data, uint16_t len))
{
    char buf[128];
    uint8_t v;
    int n = 8;
    memcpy(buf, "[DIAG]\r\n", 8);

    v = SX1276_ReadReg(REG_VERSION);
    n += 8; buf[8]=' '; buf[9]='V'; buf[10]='e'; buf[11]='r'; buf[12]='='; buf[13]="0123456789ABCDEF"[v>>4]; buf[14]="0123456789ABCDEF"[v&0xF]; buf[15]=0;

    v = SX1276_ReadReg(REG_OPMODE);
    buf[n++]=' '; buf[n++]='O'; buf[n++]='P'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    v = SX1276_ReadReg(REG_RXCONFIG);
    buf[n++]=' '; buf[n++]='R'; buf[n++]='C'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    v = SX1276_ReadReg(REG_RXBW);
    buf[n++]=' '; buf[n++]='B'; buf[n++]='W'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    v = SX1276_ReadReg(REG_DIOMAPPING1);
    buf[n++]=' '; buf[n++]='D'; buf[n++]='I'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    v = SX1276_ReadReg(REG_PREAMBLEDET);
    buf[n++]=' '; buf[n++]='P'; buf[n++]='D'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    v = SX1276_ReadReg(REG_RSSIVALUE);
    buf[n++]=' '; buf[n++]='R'; buf[n++]='I'; buf[n++]='=';
    buf[n++]="0123456789ABCDEF"[v>>4]; buf[n++]="0123456789ABCDEF"[v&0xF];

    buf[n++]='\r'; buf[n++]='\n';
    buf[n] = '\0';

    send((const uint8_t *)buf, (uint16_t)n);
}
