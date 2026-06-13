# SX1276 模块

FSK 连续接收驱动。SPI 仅用于寄存器配置，数据通过 DIO 引脚以 bit-stream 方式输出。

## 硬件接线

```
SPI:  PC5(SCK) / PC6(MOSI) / PC7(MISO) / PC4(CS)
DIO:  DIO1(PA1) = DCLK / DIO2(PA2) = DATA
其他: DIO0/3/4/5/ RESET 悬空
```

## 协议背景

依据 **TB/T 3504-2018 第 9.1 节**「接近预警信道数据传输协议」:
- 编码: POCSAG, 调制: 2FSK, 速率: 1200 bit/s
- 频点: 820.700 / 821.2375 / 821.825 MHz (三信道)
- 频偏: ±4.5 kHz, RX 带宽: 10.4 kHz SSB (等效 ~20.8 kHz)

## API 使用

```c
SX1276_Init()          → 初始化芯片、写全部寄存器、进入 FSK 连续接收
                        返回 SX1276_OK / SX1276_ERR_NO_CHIP
SX1276_SetChannel(0)   → 切换信道 (0/1/2)
SX1276_ReadRSSI()      → 读当前 RSSI (dBm, ≤ 0)
SX1276_EnterRx()       → 手动重新进入接收模式
SX1276_WriteReg/ReadReg → 调试用 SPI 寄存器读写
```

## 设计要点

- **DIO Bit-Stream 模式**: SX1276 工作在 FSK 连续模式，内部 Bit Synchronizer 恢复出 DCLK (PA1) 和 DATA (PA2)。MCU 在 1200Hz 中断中逐 bit 采集，不走 FIFO。
- **前导码检测门控**: 启用前导码检测 (`0xAA`)，只在检测到有效信号时才输出 DCLK/DATA。
- **同步字**: 由 POCSAG 层软件搜索 `0x7CD215D8`，SX1276 侧 `RegSyncConfig=0x00` 关闭硬件检测。

## 射频寄存器

| 参数 | 值 | 寄存器 |
|------|-----|--------|
| 频率 | 821.2375 MHz (CH1) | FrF = 0xCD4F33 |
| 比特率 | 1200 bps | Bitrate = 0x682B |
| 频偏 | ±4.5 kHz | Fdev = 0x4A |
| RX 带宽 | 10.4 kHz SSB (等效 ~20.8 kHz) | RxBw = 0x15 |
| AFC 带宽 | 250.0 kHz SSB (等效 ~500 kHz) | AfcBw = 0x01 |
| 前导码检测 | 2 bytes, tol 10 | 0xAA |
| DIO 映射 | DIO1=DCLK, DIO2=DATA | 0xC0 |

## 关于 DIO 映射

`RegDioMapping1 (0x40)` 的位域：`[7:6]DIO0 [5:4]DIO1 [3:2]DIO2 [1:0]DIO3`

写入 `0xC0` (`1100_0000`)：
- DIO0 = 11 → PLL_LOCK（悬空未用）
- DIO1 = 00 → **DCLK**（数据时钟）
- DIO2 = 00 → **DATA**（同步数据）
- DIO3 = 00 → DATA（悬空未用）

该值符合 SX1276 数据手册 Table 60。

## 已知问题

1. **RSSI**: 精度 ±3 dB，仅作参考。
2. **RxBw**: 建议最低 20.8 kHz (DSB)，否则 FSK ±4.5kHz 信号可能失真。