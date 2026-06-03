# GPS 模块使用说明

## 概述

本工程中 GPS 模块（DX-GP21 GNSS 定位模组）通过 USART1 与 MCU 通信，波特率 115200，8N1。  
代码实现了 NMEA-0183 协议中 **GGA** 和 **RMC** 语句的解析，并提供了缓冲器机制：  
- 若某次解析失败，保留上一次成功解析的数据。  
- 对外提供线程安全的读取接口。

## 初始化

在 `main()` 中调用 `GPS_Init()` 即可完成 USART1 及中断配置：

```c
int main(void)
{
    // ... 其他初始化 ...
    GPS_Init();             // USART1 — DX-GP21 GNSS 模组
    // ...
    while(1)
    {
        GPS_ParseNMEA();    // 定期调用解析
        // ...
    }
}
```

## 解析流程

1. **中断接收**：`USART1_IRQHandler` 将每个字节存入环形缓冲区，同时检测 `'\n'` 并填充行缓冲区 `gps_line_buf`，设置 `gps_line_ready = true`。  
2. **主循环解析**：`GPS_ParseNMEA()` 检查 `gps_line_ready`，若为 `true` 则复制行内容到临时缓冲区，依次尝试解析 GGA 和 RMC 语句。  
   - 若解析成功，更新全局缓冲器 `gps_data_buffer` 并置 `gps_data_valid = true`。  
   - 若解析失败，**保留上一次有效数据**（缓冲器不变）。  
3. **建议调用频率**：每 10~100 ms 调用一次 `GPS_ParseNMEA()`，避免错过行数据。

## 读取数据

使用 `GPS_GetLatestData()` 获取最新有效数据：

```c
GPS_Data_TypeDef gps_data;
if (GPS_GetLatestData(&gps_data))
{
    // 数据有效，可使用 gps_data.latitude, gps_data.longitude,
    // gps_data.year, gps_data.month, gps_data.day,
    // gps_data.hour, gps_data.minute, gps_data.second,
    // gps_data.timestamp, gps_data.valid
}
else
{
    // 尚无有效数据
}
```

### 数据结构体字段说明

| 字段        | 类型     | 说明                                 |
|-------------|----------|--------------------------------------|
| `latitude`  | `double` | 纬度，度格式（如 29.345612）         |
| `lat_dir`   | `char`   | 'N' 或 'S'                           |
| `longitude` | `double` | 经度，度格式（如 104.712345）        |
| `lon_dir`   | `char`   | 'E' 或 'W'                           |
| `year`      | `uint16_t` | 年（如 2026）                       |
| `month`     | `uint8_t`  | 月（1-12）                          |
| `day`       | `uint8_t`  | 日（1-31）                          |
| `hour`      | `uint8_t`  | 时（北京时间 0-23）                 |
| `minute`    | `uint8_t`  | 分（0-59）                          |
| `second`    | `uint8_t`  | 秒（0-59）                          |
| `timestamp` | `uint32_t` | UTC 时间戳（秒）                    |
| `valid`     | `bool`     | 定位是否有效                         |

## 清除缓冲数据

调用 `GPS_ClearData()` 可将缓冲器置为无效状态，并清零所有字段。

## 潜在问题与注意事项

### 1. 行缓冲区大小限制
- 行缓冲区 `gps_line_buf` 大小为 **128 字节**（`GPS_LINE_BUF_SIZE`）。  
- 若 NMEA 语句长度超过 128 字节（极少数情况），会导致溢出并丢失该行数据。  
- 如需支持更长语句，可增大 `GPS_LINE_BUF_SIZE`。

### 2. 解析仅支持 GGA 和 RMC
- 当前仅解析 `$GPGGA` / `$GNGGA` 和 `$GPRMC` / `$GNRMC` 语句。  
- 若模组输出其他语句（如 `$GPZDA`），将被忽略。  
- 如需支持更多语句，需在 `GPS_ParseNMEA()` 中添加对应解析函数。

### 3. 北京时间转换的局限性
- 时间转换仅处理 UTC+8 跨日情况，未考虑跨月/跨年的复杂边界（如 23:59:59 UTC → 07:59:59 次日）。  
- 当前实现中，若跨日导致月份/年份变化，仅做了简单处理（参考 `parse_RMC` 中的逻辑）。  
- 对于高精度应用，建议使用更完善的日期库或自行扩展。

### 4. 时间戳计算未考虑闰秒
- `timestamp` 字段基于简单天数累加，未考虑闰秒。  
- 对于大多数应用（误差 < 1 秒）可接受。

### 5. 中断优先级
- USART1 中断优先级为 `PreemptionPriority=1, SubPriority=1`。  
- 若与其他高优先级中断（如 SysTick）冲突，需调整优先级分组或数值。

### 6. 调试输出
- 当前代码未在解析成功/失败时输出调试信息。  
- 如需调试，可在 `GPS_ParseNMEA()` 中添加 `printf` 或通过 BLE 发送日志。

### 7. 内存占用
- 环形缓冲区 `gps_rx_ring` 占用 512 字节，`gps_tx_ring` 占用 128 字节。  
- 行缓冲区 `gps_line_buf` 占用 128 字节。  
- 全局结构体 `gps_data_buffer` 占用约 40 字节。  
- 总计约 808 字节 RAM，在 CH32V00X 系列中可接受。

## 示例：主循环中定期解析并发送数据

```c
#include "GPS.h"
#include "BLE.h"

int main(void)
{
    // ... 初始化 ...
    GPS_Init();
    BLE_Init();

    while(1)
    {
        GPS_ParseNMEA();    // 解析最新行

        GPS_Data_TypeDef gps;
        if (GPS_GetLatestData(&gps) && gps.valid)
        {
            // 通过 BLE 发送数据
            char buf[128];
            sprintf(buf, "GPS: %.6f%c, %.6f%c, %04d-%02d-%02d %02d:%02d:%02d\r\n",
                    gps.latitude, gps.lat_dir,
                    gps.longitude, gps.lon_dir,
                    gps.year, gps.month, gps.day,
                    gps.hour, gps.minute, gps.second);
            BLE_SendString((uint8_t*)buf);
        }

        Tick_DelayMs(100);  // 每 100ms 解析一次
    }
}
```

---

*文档版本：v1.0*  
*最后更新：2026-06-03*
