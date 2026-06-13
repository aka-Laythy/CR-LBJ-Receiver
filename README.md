# CR-LBJ-Receiver — 列车接近预警（LBJ）接收机

基于 **CH32V005F6P6** 单片机 + **SX1276** 射频模组的 POCSAG/LBJ 解码接收机，用于接收铁路列车接近预警（LBJ）信号，通过蓝牙输出解码信息并支持 OLED 显示、GPS 定位、W25Q64 数据记录等。

\---

## 📁 仓库结构

```
CR-LBJ-Receiver/
├── Code/                ← 单片机固件源码（见下方模块说明）
└── Hardware/            ← 硬件设计文件（PCB、原理图等）
```

\---

## 📟 Code — 固件说明

### 硬件平台

|项目|规格|
|-|-|
|**MCU**|CH32V005F6P6，RISC-V 内核，48MHz|
|**射频**|SX1276 LoRa/FSK 模组（2FSK 接收，DIO Bit-Stream 方案）|
|**蓝牙**|XY-MBO35A（USART2，PD2/PD3，921600bps）|
|**GPS**|DX-GP21 GNSS 模组（USART1，PD5/PD6，NMEA）|
|**存储**|W25Q64 SPI Flash（8MB，字库/配置/LBJ数据）|
|**显示**|128×64 OLED（软件 I2C，PC1/PC2）|
|**按键**|K1(PD4)、K2(PD0)、K3(PC0)|

### 软件架构

```
main()
├── Tick_Init()          ← SysTick 1ms 嘀嗒时钟
├── Key_Init()           ← 按键读取
├── BLE_Init()           ← 蓝牙初始化（USART2 中断收发）
├── GPS_Init()           ← GPS 初始化（USART1 中断收发）
├── SPI_Bus_Init()       ← SPI 总线初始化（PC5/6/7）
├── SX1276_Init()        ← SX1276 初始化 → FSK 连续接收模式
├── SPI_Flash_Init()     ← W25Q64 检测与初始化
├── FlashBurn_Init()     ← Flash 烧录协议初始化
├── bit_capture_init()   ← PA1(DCLK)/PA2(DATA) EXTI 中断捕获
├── POCSAG_Init()        ← POCSAG 解码器启动
├── LBJ_Init()           ← LBJ 解析模块启动
├── Menu_Init()          ← OLED 菜单初始化
└── schedule_init()      ← 任务调度器启动

main loop:
├── schedule_poll()          ← 轮询所有定时任务
│   ├── task: GPS_ParseNMEA  ← 解析 GPS NMEA 报文
│   │   └── 更新 GPS 数据（时间/坐标/定位有效标志）
│   ├── task: POCSAG_Process ← 逐 bit 解码 POCSAG 帧
│   │   └── → 回调 on_pocsag_message → LBJ_ParsePOCSAG
│   │       └── → 回调 on_lbj_message（BLE 输出解码结果）
│   ├── task: Menu_Task      ← OLED 菜单刷新
│   └── task: FlashBurn_Task ← 蓝牙字库烧录服务
└── ... idle
```

### 模块说明

|模块|文件|说明|
|-|-|-|
|**SX1276**|`User/SX1276/`|射频配置 & FSK 连续接收；仅初始化用 SPI，接收靠 DIO 中断|
|**Bit Capture**|`User/bit_capture.c`|PA1(DCLK)/PA2(DATA) EXTI 中断，逐 bit 组装为 byte|
|**POCSAG**|`User/POCSAG/`|POCSAG 解码器（512/1200/2400bps），按 TB/T 3504-2018|
|**LBJ**|`User/LBJ/`|LBJ 协议解析（基本帧 RIC 1234000，扩展帧 1234002）|
|**BLE**|`User/BLE.c`|XY-MBO35A 蓝牙驱动（USART2 中断，环形缓冲区）|
|**GPS**|`User/GPS.c`|DX-GP21 GNSS 驱动（USART1，NMEA-0183 解析）|
|**OLED**|`User/OLED/`|128×64 OLED 驱动 + 多级菜单系统（含 GB2312 字库）|
|**Key**|`User/Key.c`|三按键输入，上拉 + 硬件消抖|
|**SPI Bus**|`User/SPI_Bus/`|SPI 总线抽象层，软件 CS 控制（SX1276/PC4，W25Q64/PC3）|
|**SPI Flash**|`User/SPI_Flash/`|W25Q64 驱动 + 分区管理（配置/字库/LBJ数据）|
|**Flash Burn**|`User/FlashBurn/`|蓝牙无线烧录字库到 SPI Flash|
|**Schedule**|`User/schedule.c`|轻量任务调度器（最多 8 个定时任务）|
|**Tick**|`User/Tick.c`|SysTick 1ms 基准时钟|
|**str_util**|`User/str_util.c`|字符串工具（BCD ↔ 数字、格式化等）|

### 开发工具

* **IDE**: MounRiver Studio II（MRS2）
* **工具链**: RISC-V GCC
* **调试**: BLE 蓝牙透传（USART2 串口）

### 引脚分配

|功能|引脚|外设|
|-|-|-|
|OLED SCL|PC2|GPIO 输出（软件 I2C）|
|OLED SDA|PC1|GPIO 输出（软件 I2C）|
|SX1276 DCLK|PA1|EXTI（上升沿触发）|
|SX1276 DATA|PA2|EXTI（电平捕获）|
|SX1276 CS|PC4|GPIO 软件 CS|
|W25Q64 CS|PC3|GPIO 软件 CS|
|SPI SCLK|PC5|SPI|
|SPI MOSI|PC6|SPI|
|SPI MISO|PC7|SPI|
|BLE TX (→模组 RX)|PD2|USART2\_TX (AF3)|
|BLE RX (←模组 TX)|PD3|USART2\_RX (AF3)|
|GPS TX (→模组 RX)|PD5|USART1\_TX|
|GPS RX (←模组 TX)|PD6|USART1\_RX|
|K1|PD4|GPIO 输入|
|K2|PD0|GPIO 输入|
|K3|PC0|GPIO 输入|

\---

## 🖨️ Hardware — 硬件设计

*（待补充 — PCB 原理图、BOM、Layout 文件）*

\---

## ⚠️ 免责声明

本项目仅用于**技术学习与研究**，不得用于任何违反铁路安全规定的用途。使用前请遵守当地法律法规。

