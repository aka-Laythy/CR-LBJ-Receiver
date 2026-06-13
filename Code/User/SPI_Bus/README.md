# SPI\_Bus 模块说明

## 功能概述

统一管理 CH32V005F6P6 唯一的 SPI1 外设，为 SX1276 和 W25Q64 提供总线仲裁与 GPIO 软件片选。

## 引脚接线（来自 AGENTS.md）

|信号|引脚|说明|
|-|-|-|
|SPI1\_SCK|PC5|时钟，默认位置|
|SPI1\_MOSI|PC6|主机输出从机输入，默认位置|
|SPI1\_MISO|PC7|主机输入从机输出，默认位置|
|SX1276 CS|PC4|普通 GPIO，软件控制|
|W25Q64 CS|PC3|普通 GPIO，软件控制|

> \*\*注意\*\*：CH32V005F6P6 的 SPI1 默认位置就是 PC5/PC6/PC7，\*\*不需要 AFIO 重映射\*\*。

## 使用方式

```c
#include "spi\_bus.h"

// 1. 初始化 SPI 总线（必须在任何外设驱动之前调用）
SPI\_Bus\_Init();

// 2. 操作 SX1276
SPI\_Bus\_Select(SPI\_CS\_SX1276);
SPI\_Bus\_Write(tx\_buf, len);
SPI\_Bus\_Read(rx\_buf, len);
SPI\_Bus\_Release();

// 3. 操作 W25Q64
SPI\_Bus\_Select(SPI\_CS\_W25Q64);
SPI\_Bus\_Transfer(tx\_buf, rx\_buf, len);
SPI\_Bus\_Release();
```

## 主要 API

|函数|说明|
|-|-|
|`SPI\_Bus\_Init(void)`|初始化 SPI1 + CS GPIO，进入主模式|
|`SPI\_Bus\_Select(SPI\_CS\_Device\_t dev)`|选中设备，自动释放上一设备|
|`SPI\_Bus\_Release(void)`|释放当前选中设备，CS 拉高|
|`SPI\_Bus\_TransferByte(uint8\_t tx)`|单字节全双工|
|`SPI\_Bus\_Transfer(tx, rx, len)`|多字节全双工|
|`SPI\_Bus\_Write(data, len)`|只发不收|
|`SPI\_Bus\_Read(buf, len)`|只收不发（发送 0xFF）|

## 已知限制与潜在风险

1. **SPI 总线共享**：SX1276 和 W25Q64 分时复用总线，两次操作之间必须通过 `SPI\_Bus\_Release()` 释放 CS，否则会总线挂起。
2. **中断安全**：当前是阻塞式轮询实现，不能在中断服务程序里长时间调用（尤其是 `SPI\_Bus\_Transfer` 大数据时）。
3. **速度上限**：PCLK/8 = 6 MHz（@48MHz 系统时钟），W25Q64 最高支持 104 MHz（SPI），SX1276 的 FSK 模式甚至可以到更低。如需提速可改为 `SPI\_BaudRatePrescaler\_4`。
4. **CS 时序**：SX1276 和 W25Q64 都要求 CS 建立/保持时间，模块内部不做额外延时，用户如需延时应在 `SPI\_Bus\_Select` / `SPI\_Bus\_Release` 后自行加。
5. **DX-GP21 不在此模块**：DX-GP21 是 USART 串口，有独立驱动，不要误调用 SPI 模块。

