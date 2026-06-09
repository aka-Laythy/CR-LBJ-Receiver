# POCSAG 模块

POCSAG 寻呼协议解码器，适配 **TB/T 3504-2018 第 9.1 节** 的国铁 LBJ 规范。

## 协议差异 (TB/T vs 标准 POCSAG)

| 项目 | 标准 POCSAG | TB/T 3504-2018 |
|------|------------|----------------|
| BCD 映射 | A=空格, B=U, C=-, D=), E=], F=[ | C=空格, D=负号-, E=(, F=) |
| 字符内 bit 序 | MSB-first | **LSB-first** |
| 同步字 | 0x7CD215D8 | 0x7CD215D8 (一致) |
| 前导码 | 576 bit 1010... | 576 bit 1010... (一致) |
| 消息帧 | 连续 batch | 7 个码组/基本帧 |

## API 使用

```c
POCSAG_Init(my_callback)    // 注册解码回调
POCSAG_FeedByte(byte)       // 逐字节喂入 (来自 bit_capture)
POCSAG_Process()            // 每 ~20ms 调用: 超时检查 + 滑动搜索同步字
POCSAG_Reset()              // 清空缓冲区
```

### 回调原型

```c
void callback(uint32_t ric, uint8_t function,
              const char *text, uint8_t len, int16_t rssi);
```

`text` 为 TB/T BCD 解码结果（已做 nibble 反转 + 映射），以 `\0` 结尾。

## 设计要点

- **char 内 nibble 反转**: TB/T 规定每个 BCD 字符的 4 个 bit 是 LSB-first 传输的。`nibble_reverse()` 在解码前反转每个 nibble。
- **TB/T BCD 映射**: `bcd_to_char()` 使用 TB/T 专用映射表 (C=空格, D=-, E=(, F=))。
- **滑动搜索同步字**: 逐字节滑动匹配 `0x7CD215D8`，找到后按 32-bit 切分码字。
- **BCH(31,21) + 偶校验**: 支持 1-bit 纠错，不可纠正的码字静默丢弃。
- **消息超时**: 3 秒未收到后续码字则提交当前消息。

## 已知问题

1. **nibble 反转**: 如果联调时收到的字符始终是乱码，可能是 nibble 反转方向反了。尝试去掉 `nibble_reverse()`。
2. **bit 级偏移**: 前导码检测触发时如非 byte 对齐，首字节是垃圾，但 576 bit 前导码足够滑动对齐。
3. **2-bit+ 错误**: 仅做 1-bit 纠错。信道质量差时可能出现不可纠正码字，应检查射频链路。
