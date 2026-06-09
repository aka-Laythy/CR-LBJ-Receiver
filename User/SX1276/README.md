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
- 频偏: ±4.5 kHz, RX 带宽: 20.8 kHz

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
| 频率 | 821.2375 MHz (CH1) | FrF = 0x690B01 |
| 比特率 | 1200 bps | Bitrate = 0x682B |
| 频偏 | ±4.5 kHz | Fdev = 0x4A |
| RX 带宽 | 20.8 kHz | RxBw = 0x14 |
| 前导码检测 | 2 bytes, tol 10 | 0xAA |
| DIO 映射 | DIO1=DCLK, DIO2=Data | 0xC0 |

## 已知问题

1. **DIO 映射值**: 部分 SX1276 批次 DIO2=Data 需 `0xE0` 而非 `0xC0`。
2. **DCLK 边沿**: 如果 bit 流看似反了，换下降沿触发。
3. **RSSI**: 精度 ±3 dB，仅作参考。
4. **RxBw**: 切勿低于 `0x14` (20.8 kHz)，否则 FSK ±4.5kHz 信号失真。
