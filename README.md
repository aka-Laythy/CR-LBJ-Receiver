# CR-LBJ-Receiver — 列车接近预警（LBJ）接收机

基于 **CH32V005F6P6** 单片机 + **SX1276** 射频模组的 POCSAG/LBJ 解码接收机，用于接收铁路列车接近预警（LBJ）信号，通过蓝牙输出解码信息并支持 OLED 显示、GPS 定位、W25Q64 数据记录等。

\---

## 📁 仓库结构

```
CR-LBJ-Receiver/
├── Code/                ← 单片机固件源码
└── Hardware/            ← 硬件设计文件（PCB、原理图等）
```

\---

## 📟 Code — 固件说明

### 数据流

```
┌─────────────┐     ┌──────────────┐     ┌────────────┐     ┌───────┐
│  SX1276     │────→│ bit\_capture  │────→│  POCSAG    │────→│  LBJ  │──→ BLE 输出
│  FSK 连续RX │DCLK │  ISR 逐 bit  │ byte│  解码器    │text │ 解析器│
│             │DATA │  组装 byte   │     │            │     │       │
└─────────────┘     └──────────────┘     └────────────┘     └───────┘
                           ↑ EXTI 1200Hz                     ↑ RIC=1234000
```

### 调度

|任务|周期|内容|
|-|-|-|
|task0|10ms|GPS NMEA 解析|
|task1|20ms|bit\_capture 取字节 → POCSAG\_FeedByte → POCSAG\_Process，每 5s 输出 RSSI/EXTI 心跳|
|Menu\_Task|10ms|OLED 菜单刷新 \& 按键检测|
|FlashBurn\_Task|10ms|蓝牙-SPI-FLASH烧录服务|

### 模块说明

|模块|职责|
|-|-|
|`SX1276/`|SX1276 SPI 配置、FSK 连续模式、DIO 映射、三信道切换（820.7/821.2375/821.825 MHz）、Flash 持久化带宽配置|
|`bit\_capture.c`|PA1(DCLK) EXTI 上升沿中断，逐 bit MSB 优先组装为 byte，128 字节环形缓冲区|
|`POCSAG/`|POCSAG 解码：BCH(31,21)+偶校验纠错、同步字 0x7CD215D8 滑窗搜索、TB/T BCD 映射（A→\* / B→U / C→空格 等）|
|`LBJ/`|TB/T 3504 固定格式解析：基本帧 RIC 1234000（\[5C 车次]\[3C 速度]\[5C 公里标]）、扩展帧 RIC 1234002（机车类型/编号/线路名）、Session 管理|
|`BLE.c`|XY-MBO35A 蓝牙驱动（USART2\_AF3 PD2/PD3，初始 115200 → AT 命令 → 921600），非阻塞环形缓冲区|
|`GPS.c`|DX-GP21 GNSS 驱动（USART1 PD5/PD6，115200），NMEA RMC/GGA 解析，纯整数无浮点，UTC+8 北京时间|
|`OLED/`|128×64 OLED 软件 I2C 驱动（PC1 SDA / PC2 SCL），显存 8×128，含 GB2312 字库显示|
|`Menu.c`|多级菜单系统：8 项主菜单，K1上/K2下/K3确认，水平滚动，射频调试页支持 BW 编辑\&持久化|
|`Key.c`|三按键输入（K1=PD4 / K2=PD0 / K3=PC0，上拉输入）|
|`SPI\_Bus/`|共享 SPI 总线抽象层（PC5 SCK / PC6 MOSI / PC7 MISO），软件 CS 控制（SX1276=PC4 / W25Q64=PC3）|
|`SPI\_Flash/`|W25Q64 驱动 + 分区管理（配置 16KB / 字库 1MB / LBJ 数据 \~5.9MB），含 GB2312 字库读取|
|`FlashBurn/`|蓝牙无线烧录字库到 SPI Flash：行命令协议（fire font / BURN\_DATA / BURN\_VERIFY），256 字节页写入|
|`schedule.c`|轻量任务调度器（最多 8 个任务），基于 tick\_ms 无符号差值防溢出，超 2 周期自动重置基准|
|`Tick.c`|SysTick 1ms 基准时钟（tick\_ms），HCLK=48MHz CMP=47999|
|`str\_util.c`|轻量字符串工具：utoa/itoa、hex 格式化、mini\_sprintf（支持 %d/%u/%02X）、整数 atan2 查表|
|`ch32v00X\_it.c`|EXTI7\_0 中断 → bit\_capture\_isr()；HardFault → NVIC\_SystemReset|

### 开发工具

* **IDE**: MounRiver Studio II（MRS2）
* **工具链**: RISC-V WCH GCC12
* **MCU**: CH32V005F6P6，32KB Flash / 6KB RAM，48MHz HSI
* **调试**: BLE 蓝牙透传（USART2 串口）

### 调试输出

1. **确认硬件** — 上电后蓝牙配对，应看到：

```
   \[OK] SX1276 init done
   \[DIAG] Ver=12 OP=0E RC=08 BW=14 DI=C0 PD=AA RI=xx
   ```

   * `Ver`=0x12/0x22 芯片正常；`OP`=0x0E 表示 FSK 连续 RX 模式；`DI`=0xC0 表示 DIO1=DCLK、DIO2=DATA
   * 无 DIAG → 检查 SX1276 供电和 SPI 接线
2. **验证射频接收** — 每 5 秒输出心跳：

```
   \[HB] RSSI=-108 EXTI=0Hz Ovf=0 Msgs=0
   ```

   * `RSSI`：-120\~-50 dBm，始终 -128 检查天线和频点
   * `EXTI`：DCLK 中断频率，收到信号时应接近 1200Hz
   * `Ovf`：缓冲区溢出次数，正常 0
   * `Msgs`：已收到的 LBJ 消息总数
3. **验证解码** — 收到 LBJ 信号时：

```
   \[LBJ] RIC=1234000 Fn=3 RSSI=-98
     Train=12345 (even)  Speed=120 km/h  KM=1234
     Raw=12345120 1234
   ```

### 协议标准

TB/T 3504-2018 第 9.1 节「接近预警信道数据传输协议」。

\---

## 🖨️ Hardware — 硬件设计

### 硬件平台

|项目|规格|
|-|-|
|**MCU**|CH32V005F6P6，RISC-V 内核，48MHz|
|**射频**|SX1276 LoRa/FSK 模组（2FSK 接收，DIO Bit-Stream 方案）|
|**蓝牙**|XY-MBO35A（USART2，PD2/PD3，921600bps）|
|**GPS**|DX-GP21 GNSS 模组（USART1，PD5/PD6，NMEA）|
|**存储**|W25Q64 SPI Flash（8MB，字库/配置/LBJ 数据）|
|**显示**|128×64 OLED（软件 I2C，PC1/PC2）|
|**按键**|K1(PD4)、K2(PD0)、K3(PC0)|

### 模组接线

|模组|接口|MCU 引脚|备注|
|-|-|-|-|
|SX1276 (Ra-01H)|SPI|PC5(SCK) / PC6(MOSI) / PC7(MISO) / PC4(CS)|与 W25Q64 共用 SPI 总线|
|SX1276 DIO|GPIO|PA1(DIO1/DCLK) / PA2(DIO2/DATA)|DIO0/3/4/5 悬空|
|DX-GP21 (GNSS)|USART1|PD5(TX) / PD6(RX)|WAKE\_UP/RESET/ANT\_ON/SET 悬空；VCC\_Backup 纽扣电池供电|
|XY-MBO35A (BLE)|USART2\_AF3|PD2(TX) / PD3(RX)|BRTS 接地不休眠；RST/CTS/LINK 悬空|
|W25Q64 (Flash)|SPI|PC3(CS)，共用 SPI 总线|WP# / HOLD# 固定上拉|
|OLED 128×64|软件 I2C|PC1(SDA) / PC2(SCL)|开漏输出 + 外部上拉|

### 电源

*（待补充 — 供电方案、电压调节、功耗数据）*

### 原理图

*（待补充 — 完整原理图 PDF / 源文件）*

### PCB Layout

*（待补充 — PCB 文件、Gerber、BOM）*

\---

## ⚠️ 免责声明

本项目仅用于**技术学习与研究**，不得用于任何违反铁路安全规定的用途。使用前请遵守当地法律法规。

