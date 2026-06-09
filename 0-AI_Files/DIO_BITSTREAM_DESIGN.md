# SX1276 DIO Bit-Stream 接收方案设计文档（修订版）

> 本文档描述从 SPI+FIFO 方案切换到 **DIO1/DIO2 → MCU GPIO** 接收 FSK 解调后原始 bit 流的硬件架构与软件路径。
>
> 适用场景：国铁 LBJ（POCSAG 1200bps / 821.2375MHz）接收。
>
> 适用模组：Ra-01H（基于 Semtech SX1276）。

---

## 0. 方案结论

**SPI 仅用于 SX1276 寄存器配置，数据接收走 DIO 引脚。**

| 方式 | 用途 | 说明 |
|------|------|------|
| SPI (PC5/6/7 + PC4 CS) | **初始化时**写寄存器（频率/带宽/DIO映射等），之后可读取 RSSI | 进入接收模式后 SPI 总线静默 |
| DIO1 (飞线 → MCU GPIO) | **DCLK**：SX1276 内部 Bit Synchronizer 恢复出的数据时钟 | 1200Hz 方波，上升沿时 DIO2 数据有效 |
| DIO2 (飞线 → MCU GPIO) | **DATA**：FSK 解调 + Bit Synchronizer 后的同步数据流 | MCU 在 DIO1 上升沿中断中读取 |

**不需要读 FIFO**。SX1276 工作在 FSK 连续模式（Continuous Mode），解调后的 bit 直接从 DIO2 输出，DIO1 提供同步时钟。

---

## 1. 硬件接线

### 1.1 新增飞线（2 根）

| SX1276 模组端 | MCU (CH32V005F6P6) 端 | 功能 |
|:---|:---|:---|
| DIO1（模组 Pin 9） | **PA1**（EXTI1，上升沿中断） | DCLK — 数据时钟输入 |
| DIO2（模组 Pin 10） | **PA2**（普通 GPIO 输入） | DATA — 同步数据流 |

> **引脚选择理由**：
> - 项目使用 HSI 内部振荡器（非 HSE 外部晶振），PA1/PA2 不用于 OSC_IN/OSC_OUT，释放为普通 GPIO。
> - `AFIO_PCFR1 bit17` 在 HSI 模式下未置位，PA1/PA2 默认即为 GPIO 功能（`system_ch32v00X.c` 中 SetSysClockTo_xxMHz_HSI 不含 PA1/PA2 关闭操作）。
> - PA1 对应 EXTI_Line1，PA2 对应 EXTI_Line2，共享中断向量 `EXTI7_0_IRQn`。
> - 不与现有外设冲突：SPI(PC3~7)、USART1(PD5/6)、USART2(PD2/3)。
> - Ra-01H 模组 DIO 引脚参考：DIO0=Pin8, DIO1=Pin9, DIO2=Pin10, DIO3=Pin11, DIO4=Pin12, DIO5=Pin13（以模组丝印为准）。

### 1.2 现有接线（保持不变）

```
SPI:
  PC5 ──→ SCK
  PC6 ──→ MOSI
  PC7 ──→ MISO
  PC4 ──→ SX1276 CS

新增飞线:
  PA1 ──→ SX1276 DIO1 (DCLK)
  PA2 ──→ SX1276 DIO2 (DATA)

供电:
  3.3V ──→ VCC
  GND  ──→ GND

悬空:
  DIO0, DIO3, DIO4, DIO5, RESET
  （RESET 可接 GPIO 做硬复位，非必须）
```

---

## 2. SX1276 寄存器配置

### 2.1 DIO 映射（关键）

SX1276 的 DIO 映射寄存器 `RegDioMapping1 (0x40)` 在不同批次/版本中，DIO2 的 "Data" 映射值可能不同：

| 寄存器值 | DIO1 功能 | DIO2 功能 | 适用情况 |
|:---|:---|:---|:---|
| **`0xC0`**（首选） | DCLK | Data | 多数 SX1276 批次，DIO2[5:4]=`00` 映射为 Data |
| **`0xE0`**（备选） | DCLK | Data | 部分批次 DIO2[5:4]=`10` 映射为 Data |

> **调试方法**：写入 `0xC0` 后进入接收模式，用逻辑分析仪/示波器测量 DIO1 和 DIO2：
> - DIO1 应有 1200Hz 方波（收到信号时）或空闲电平（无信号时，需前导码检测开启）。
> - DIO2 应有电平跳变（与 DCLK 同步的数据）。
> - 若 DIO2 始终为高/低不变化，则改试 `0xE0`。

`RegDioMapping2 (0x41)` 保持 `0x00`（默认）。

### 2.2 FSK 连续接收模式（核心寄存器序列）

```
操作顺序（写寄存器）:

1. Sleep:      RegOpMode        = 0x00    (FSK + Sleep)
   延时 2ms
2. Stdby:      RegOpMode        = 0x01    (FSK + Stdby)
   延时 2ms
3. 频率:       RegFrF[MSB:MID:LSB] = frf   (821.2375 MHz)
4. 比特率:     RegBitrate[MSB:LSB] = 0x682B (1200 bps, Fxosc=32MHz)
5. 频偏:       RegFdev[MSB:LSB]    = 0x004A (±4.5 kHz, 61Hz步进)
6. RX带宽:     RegRxBw          = 0x06    (20.8 kHz @ Fxosc=32MHz)
7. AFC带宽:    RegAfcBw         = 0x06    (20.8 kHz)
8. LNA:        RegLna           = 0x20    (最大增益, LnaBoost OFF)
9. RX配置:     RegRxConfig      = 0x08    (RestartRxWithoutPllLock=1)
10. 前导码检测: RegPreambleDetect = 0xAA    (❗ 注意：bit7=1 才启用)
11. 关闭同步字: RegSyncConfig     = 0x00    (软件自行搜索同步字)
12. 包配置:     RegPacketConfig1  = 0x00    (固定长度, 无CRC, 无DC-free)
13. 载荷长度:   RegPayloadLength  = 0x40    (64, 不影响连续模式)
14. DIO映射:    RegDioMapping1   = 0xC0    (DIO1=DCLK, DIO2=Data)
15. 进入RX:     RegOpMode        = 0x0E    (FSK + RX Continuous)
```

### 2.3 关键寄存器详解

#### RegPreambleDetect (0x1F) — ⚠️ 之前代码有误

```
bit[7]   = 1  ← 必须置 1，启用前导码检测器
bit[6:5] = 01 ← 前导码检测长度: 2 bytes
bit[4:0] = 01010 ← 容忍错误数: 10

∴ 正确值 = 0xAA (10101010)
```

之前代码写入了 `0x2A`（`bit7=0`），**前导码检测器实际是关闭的**。关闭的后果：
- DCLK 在有信号/无信号时都持续输出，产生大量无效中断。
- DIO2 在无信号时输出噪声，增加软件过滤负担。

启用后（`0xAA`）：
- SX1276 只在检测到有效前导码（0x55/0xAA 交替）后才从 DIO 输出数据。
- 无信号时 DCLK 空闲，不会触发 EXTI 中断。
- POCSAG 前导码有 576 bits，远超过检测所需（~16 bits），不会丢数据。

#### RegRxConfig (0x0D)

```
bit[4] RestartRxWithPllLock    = 0
bit[3] RestartRxWithoutPllLock = 1  ← 接收机可不等PLL锁定就重启
bit[2] RxTrigger               = 0  ← 连续模式不使用触发
其余   保留                     = 0

∴ 值 = 0x08
```

#### RegSyncConfig (0x27) = 0x00

关闭 SX1276 内部的 Sync Word 硬件检测。所有数据（包括前导码、同步字、码字）都从 DIO 输出，MCU 软件负责搜索 POCSAG 同步字 `0x7CD215D8`。

### 2.4 射频参数

| 参数 | 值 | 寄存器/计算 |
|------|-----|-----------|
| 频率 | 821.2375 MHz | `frf = 821237500 × 2^19 / 32000000 = 0x690B01` |
| 比特率 | 1200 bps | `bitrate = 32000000 / 1200 = 26667 = 0x682B` |
| 频偏 | ±4.5 kHz | `fdev = 4500 / 61.035 = 74 = 0x4A` |
| RX 带宽 | 20.8 kHz | `RxBwMant=24, RxBwExp=5` → `0x06`（详见手册 Table xx） |
| AFC 带宽 | 20.8 kHz | 同 RX 带宽 |
| LNA 增益 | 最高 | `LnaGain=001 (G1)`, `LnaBoost=0` → `0x20` |

### 2.5 三信道支持

LBJ 使用三个信道，切换时只需重新写入频率寄存器并重新进入 RX：

| 信道 | 频率 | FrF 寄存器值 |
|------|------|-------------|
| CH0 | 820.7000 MHz | `frf = 820700000 × 2^19 / 32000000` |
| CH1 | 821.2375 MHz | 默认 |
| CH2 | 821.8250 MHz | `frf = 821825000 × 2^19 / 32000000` |

---

## 3. SX1276 Bit Synchronizer 工作原理

### 3.1 自动运行

Bit Synchronizer 在 FSK 连续接收模式 + DIO1 映射为 DCLK 时**自动启用**，无需额外寄存器配置。

```
┌────────────────────────────────────────────────────┐
│                   SX1276 内部                      │
│                                                    │
│  RF_IN ─→ LNA ─→ Mixer ─→ IF_Filter ─→ FSK_Demod │
│                                              │     │
│                                              ▼     │
│                                    ┌─────────────────┐   │
│                                    │  Bit Sync        │   │
│                                    │  (Clock Recovery) │   │
│                                    └──────┬─────┬────┘   │
│                                           │     │       │
│                                    DCLK ──┘     └── DATA│
│                                    (DIO1)        (DIO2) │
└────────────────────────────────────────────────────┘
```

- Bit Synchronizer 从 FSK 解调后的基带信号中恢复出同步时钟。
- DIO2/DATA 输出的是**与 DCLK 同步后的数据**（已消除抖动）。
- DATA 在 DCLK 的**上升沿**有效（以 SX1276 datasheet 为准，实机需验证）。

### 3.2 前导码检测门控

启用前导码检测（`RegPreambleDetect bit7=1`）后：
- SX1276 持续监听，但**不输出 DCLK 和 DATA**。
- 检测到有效前导码后，开始输出 DCLK+DATA。
- 信号丢失后，经过一个短暂的保持时间（hysteresis），停止输出。

### 3.3 时钟极性

如果收到的数据看起来是反的（0/1 颠倒），可能原因：
- FSK 频偏极性反转：可尝试 `RegPacketConfig2 (0x31)` 的 `DataMode` 位
- 或在软件中取反每个 bit
- 或改变 DCLK 的 EXTI 触发边沿（上升沿→下降沿）

POCSAG 前导码是 `01010101...` 模式，可通过观察原始 bit 流验证极性。

---

## 4. MCU 侧软件架构

### 4.1 总体数据流

```
┌──────────┐  DCLK/ DATA  ┌─────────────┐
│  SX1276  │ ────────────→ │  MCU GPIO   │
│  (FSK RX)│               │  PA1/PA2    │
└──────────┘               └──────┬──────┘
                                  │
                    EXTI1 中断 (DCLK 上升沿)
                    读 PA2 (DATA) 存入 bit_buf
                                  │
                                  ▼
```

### 4.2 GPIO 中断采集

**DIO1 (DCLK)** → PA1 → EXTI1，配置为**上升沿触发**。

> 也可以下降沿触发。关键是：在 DCLK 的有效边沿读取 DATA（DIO2/PA2）。如果数据错误，尝试换边沿。

**中断服务函数（伪代码）**：

```c
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        // 读 DIO2/DATA 引脚 (PA2)
        uint8_t bit = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) ? 1 : 0;
        bit_buf_push(bit);
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}
```

- **中断频率**：1200 Hz（仅在收到有效信号时）
- **每次 ISR 耗时**：< 1 µs（读 GPIO + 写环形缓冲 + 清标志）
- **CPU 占用**：1200Hz × ~0.5µs ≈ 0.06%（48MHz 主频，远低于 1%）

### 4.3 Bit → Byte 组装（主循环中）

```c
// 主循环中调用（如 schedule_task1 的每个 20ms 周期内）
while (bit_buf_available() >= 8) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (byte << 1) | bit_buf_pop();  // MSB-first
    }
    POCSAG_FeedByte(byte);
}
POCSAG_Process();
```

⚠️ POCSAG 是 **MSB-first** 传输，先收到的 bit 是字节的最高位。

### 4.4 EXTI 配置代码骨架

```c
void DIO_EXTI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};

    // 使能时钟（GPIOA 在 PB2 总线上）
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA | RCC_PB2Periph_AFIO, ENABLE);

    // PA1: DIO1/DCLK — 浮空输入
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA2: DIO2/DATA — 浮空输入
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_2;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 将 PA1 映射到 EXTI_Line1
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    // 配置 EXTI_Line1: 上升沿触发
    EXTI_InitStructure.EXTI_Line      = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode      = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger   = EXTI_Trigger_Rising;  // 先试上升沿
    EXTI_InitStructure.EXTI_LineCmd   = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // NVIC 配置
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI7_0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
```

### 4.5 POCSAG 前导码 → 同步字 → 码字

bit 流中的 POCSAG 结构（MSB-first）：

```text
[576 bits of preamble: ...101010101010...]
[32-bit Sync Word: 0x7CD215D8]
[CodeWord 0] [CodeWord 1] ... [CodeWord 15]   ← 一个 Batch (16码字)
[32-bit Sync Word: 0x7CD215D8]
[CodeWord 0] [CodeWord 1] ...                   ← 下一个 Batch
...
```

MCU 软件在字节流中**滑动搜索** `0x7CD215D8`：
- 找到同步字后，按 32-bit 边界切分后续码字
- 送入 BCH 纠错 + numeric 解码
- POCSAG 解码器代码（`pocsag.c`）**无需修改**

---

## 5. 新旧方案对比

| | 旧方案 (SPI+FIFO) | 新方案 (DIO Bit-Stream) |
|:---|:---|:---|
| 数据获取 | SPI 轮询读 FIFO | DIO 引脚硬接线，自动流入 |
| FIFO 限制 | 64 字节，POCSAG batch 140 字节 → 必须高频轮询，可能溢出 | **无限制**，连续流 |
| 同步字检测 | 关闭硬件 Sync，软件在 FIFO 字节流中搜索 | 关闭硬件 Sync，软件在字节流中搜索（**逻辑不变**） |
| CPU 占用 | 每 10~20ms 做一次 SPI 轮询（多字节传输） | 1200Hz EXTI 中断，每次 < 1µs |
| 时钟恢复 | SX1276 内部 Bit Sync → FIFO | SX1276 内部 Bit Sync → DCLK 输出 → MCU |
| 代码改动 | — | `sx1276.c` 精简掉 FIFO 读取函数；新增 `bit_capture.c`；POCSAG/LBJ 层不变 |

---

## 6. 调试与验证

### 6.1 硬件检查

1. 用**逻辑分析仪**同时接 DIO1(PA1) 和 DIO2(PA2)：
   - 确认 DIO1 在收到信号时有 1200Hz 方波
   - 确认 DIO2 数据在 DIO1 的边沿变化
   - 确认无信号时 DIO1 空闲（前提：前导码检测已启用）

2. 用**示波器**查看 DIO2 波形：
   - 确认有前导码的 101010... 交替模式
   - 可对比 POCSAG 理论波形

### 6.2 软件调试

1. **串口打印原始 bit**：在 EXTI ISR 中计数，每收到 1200 个 bit 打印一行
2. **串口打印原始字节**：在主循环中将组装好的字节以 hex 格式输出，验证看到 `7C D2 15 D8` 同步字
3. **RSSI 监控**：定期（如每秒）读 `RegRssiValue (0x11)`，确认信号强度

### 6.3 常见问题排查

| 问题 | 可能原因 | 解决方法 |
|:---|:---|:---|
| DIO1 无时钟 | DIO 映射值不对 | 尝试 `0xC0` 和 `0xE0` |
| DIO1 始终有时钟（无信号也不停） | 前导码检测未启用 | 检查 `RegPreambleDetect bit7=1`，值应为 `0xAA` 而非 `0x2A` |
| DIO2 数据不变 | DIO2 映射不对 | 同上，尝试 `0xC0`/`0xE0` |
| 收到数据但解析不出同步字 | bit 极性反转 | 在软件中取反 bit，或换 EXTI 触发边沿 |
| EXTI ISR 频繁触发但无有效数据 | 噪声触发 / 前导码检测阈值太低 | 增大 `RegPreambleDetect` 检测长度（bit6:5=10 → 3 bytes） |
| DCLK 频率不是 1200Hz | 比特率寄存器值不对 | 验证 `RegBitrate` = `0x682B` |

---

## 7. 待办事项

- [x] 设计文档完成
- [x] **飞线**：PA1→DIO1, PA2→DIO2
- [ ] 重写 `sx1276.c`：仅保留寄存器配置 + RSSI 读取，删除 FIFO 读取函数
- [ ] 新增 `bit_capture.c/.h`：GPIO EXTI 中断 + bit 环形缓冲区 + byte 组装
- [ ] 修改 `ch32v00X_it.c`：添加 `EXTI7_0_IRQHandler`
- [ ] 修改 `main.c`：初始化 DIO EXTI，主循环中从 bit_buf 取字节送入 POCSAG
- [ ] 联调：逻辑分析仪验证 DCLK/DATA 波形 → 串口打印原始字节 → 验证同步字检测 → 端到端解码
