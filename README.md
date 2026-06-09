# CR-LBJ-Receiver-CH32V005-proj

国铁 LBJ（列车接近预警）接收器。基于 CH32V005F6P6 + SX1276，通过 DIO bit-stream 接收 FSK POCSAG 信号，解析后经蓝牙输出。

## 硬件

| 模组 | 接口 | MCU 引脚 |
|------|------|---------|
| SX1276 (Ra-01H) | SPI | PC5(SCK) / PC6(MOSI) / PC7(MISO) / PC4(CS) |
| SX1276 DIO | GPIO | PA1(DIO1/DCLK) / PA2(DIO2/DATA) |
| DX-GP21 (GNSS) | USART1 | PD5(TX) / PD6(RX) |
| DX-BT311 (BLE) | USART2_AF3 | PD2(TX) / PD3(RX) |
| W25Q64 (Flash) | SPI | PC3(CS)，共用 SPI 总线 |

## 架构

```
┌─────────────┐     ┌──────────────┐     ┌────────────┐     ┌───────┐
│  SX1276     │────→│ bit_capture  │────→│  POCSAG    │────→│  LBJ  │──→ BLE 输出
│  FSK 连续RX │DCLK │  ISR 逐 bit  │ byte│  解码器    │text │ 解析器│
│             │DATA │  组装 byte   │     │            │     │       │
└─────────────┘     └──────────────┘     └────────────┘     └───────┘
                           ↑ EXT1 1200Hz                     ↑ RIC=1234000
```

### 调度

- **task0 (10ms)**：GPS NMEA 解析
- **task1 (20ms)**：从 `bit_capture` 取字节 → `POCSAG_FeedByte` → `POCSAG_Process`，附带心跳输出（每 5s）

### 模块说明

| 目录 | 职责 |
|------|------|
| `User/SX1276/` | SX1276 SPI 配置、FSK 连续模式、DIO 映射、自检 |
| `User/bit_capture.c` | PA1/DCLK EXTI ISR、bit→byte 环形缓冲区 |
| `User/POCSAG/` | POCSAG 解码：BCH 纠错、同步字滑动搜索、TB/T BCD 映射 |
| `User/LBJ/` | TB/T 3504 固定格式解析：[5C 车次][3C 速度][5C 公里标] |
| `User/GPS.c` | GNSS NMEA 解析（纯整数，无浮点） |
| `User/BLE.c` | 蓝牙 UART 收发 |
| `User/SPI_Bus/` | 共享 SPI 总线（SX1276 + W25Q64） |
| `User/str_util.c` | 轻量 itoa/utoa（替代 sprintf） |

## 构建

MRS2 IDE（MounRiver Studio 2），RISC-V WCH GCC12 工具链。

- MCU：CH32V005F6P6，32KB Flash / 6KB RAM
- 主频：48MHz HSI

## 首次联调

### 1. 确认硬件

蓝牙串口连上后，上电应看到：
```
[OK] SX1276 init done
[DIAG]
 Ver=12 OP=0E RC=08 BW=14 DI=C0 PD=AA RI=xx
```

- `Ver` 应为 0x12 或 0x22（芯片类型）
- `OP` 应为 0x0E（FSK RX 连续模式）
- `DI` 应为 0xC0（DIO1=DCLK, DIO2=DATA）
- 如果 DIAG 不出现，检查 SX1276 供电和 SPI 接线

### 2. 验证射频接收

启用后每 5 秒输出心跳：
```
[HB] RSSI=-108 EXTI=0Hz Ovf=0 Msgs=0
```

- `RSSI`：信号强度，-120~-50 dBm。若始终 -128（无效），检查天线和频点。
- `EXTI`：DCLK 中断频率。收到信号时应接近 1200Hz。始终为 0 → 无信号或有信号但 DIO 映射错误。
- `Ovf`：缓冲区溢出次数，正常 0。
- `Msgs`：已收到的 LBJ 消息总数。

### 3. 验证 POCSAG 解码

收到已知的 LBJ 信号时，蓝牙输出：
```
[LBJ] RIC=1234000 Fn=3 RSSI=-98
  Train=12345 (even)
  Speed=120 km/h
  KM=1234
  Raw=12345120 1234
```

### 常见故障排查

| 现象 | 可能原因 | 解决 |
|------|---------|------|
| `Ver=0x00` 或 `0xFF` | SPI 通讯失败 | 检查 PC4/CS 和 SPI 连线 |
| EXTI 为 0Hz | 无信号 / DIO 映射错 | 用逻辑分析仪测 DIO1；尝试 `REG_DIOMAPPING1=0xE0` |
| EXTI 有数据但 Msgs=0 | BCD 格式不匹配 | 注释掉 `POCSAG_NIBBLE_REVERSE` 宏再试 |
| RSSI=-128 不变 | RSSI 寄存器不可靠 | 连续模式下 RSSI 可能不准确，不必深究 |
| 收到消息但全是乱码 | nibble 反转极性反 | 同上，注释掉 `POCSAG_NIBBLE_REVERSE` |

## 协议标准

TB/T 3504-2018 第 9.1 节「接近预警信道数据传输协议」。原始文档见 `0-AI_Files/`。

## 许可证

内部项目，未公开。
