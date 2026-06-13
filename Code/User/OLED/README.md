# Menu 模块 — OLED 菜单系统

## 文件结构

```
User/OLED/
├── Menu.h          # 公开 API
├── Menu.c          # 实现
├── README.md       # 本文档
├── OLED.h/c        # OLED 驱动（外部依赖）
└── OLED_Data.h/c   # OLED 字库（外部依赖）
```

其他外部依赖：`User/Key.h/c`（按键）、`User/Tick.h/c`（系统滴答时钟）。

---

## API 说明

### `void Menu_Init(void)`

初始化菜单模块。必须在调度器启动前调用。

- 重置选择项、滚动偏移、按键状态
- 记录 `tick_ms` 作为 splash 起始时间
- 标记需要重绘

### `void Menu_Task(void)`

周期性任务函数，建议以 ≤20ms 周期注册到调度器中。

内部状态机自动流转：`SPLASH → MENU → ENTERED`

需要在 `main.c` 中：

```c
schedule_register(10, Menu_Task);   // 注册到调度器，周期 10ms
```

---

## 集成步骤

### 1. 加入包含

```c
// main.c
#include "Key.h"
#include "OLED/Menu.h"
```

### 2. 初始化（调度器启动前）

```c
Key_Init();             // 按键 GPIO 初始化
Menu_Init();            // 菜单模块初始化（内部调用 OLED_Init）
```

### 3. 注册调度任务

```c
schedule_init(10);                      // task0 = GPS, 10ms
schedule_register(20, schedule_task1);  // task1 = SX1276, 20ms
schedule_register(10, Menu_Task);       // Menu_Task, 10ms
```

---

## 行为说明

### SPLASH 阶段

上电后立即显示居中大字 **HELLO**（OLED_8X16 字体）：

- 字符串 "HELLO" 像素宽度 = 5 × 8 = 40px
- 居中 X = (128 − 40) ÷ 2 = **44**
- 垂直居中 Y = (64 − 16) ÷ 2 = **24**

持续 **500ms** 后自动清屏，进入菜单列表。

### MENU 阶段

#### 显示布局

```
列 0   列 8          列 128
 │      │
[>]  系统状态
[ ]  频率设置
[ ]  信号检测
[ ]  版本信息
```

- **列 0**：保留给选中指示符 `>`（ASCII 0x3E），不显示文字
- **列 8**：项目文字起始位置
- 每行高度 16px（OLED_8X16），一屏最多显示 **4 项**
- 10 个项目，滚动偏移范围 0~6

#### 按键处理

| 按键 | 功能 | 边缘检测 |
|------|------|----------|
| K1（上键） | 选择上一项；若已到可见区顶部则整体上滚 | 上升沿触发，长按不重复 |
| K2（下键） | 选择下一项；若已到可见区底部则整体下滚 | 同上 |
| K3（确定键） | 进入选中项目（状态 → ENTERED） | 同上 |

滚动逻辑：

```
上滚条件：selection < scroll_offset
           → scroll_offset = selection

下滚条件：selection ≥ scroll_offset + MENU_VISIBLE_ITEMS
           → scroll_offset = selection − MENU_VISIBLE_ITEMS + 1
```

### ENTERED 阶段

按 K3 后清屏，居中显示当前项目名称（占位演示）。

暂不处理返回，后续可通过添加状态或回调函数扩展。

---

## 实现细节

### 菜单项定义

```c
static char *menu_items[MENU_ITEM_COUNT] = {
    "系统状态", "频率设置", "信号检测", "版本信息",
    "数据记录", "校准测试", "调试模式", "网络配置",
    "存储管理", "关机重启",
};
```

共 10 项，UTF-8 编码。每个中文字符 3 字节，显示为 16×16 像素。

### 键值边缘检测

```c
uint8_t k1 = Key_IsPressed(KEY_K1);
if (k1 && !prev_k1) { /* 上升沿触发 */ }
prev_k1 = k1;
```

每次 `Menu_Task` 调用时采样一次，记录上一次状态。仅当从 `0→1` 时触发动作，按住不放不会重复触发。

### 重绘优化

`needs_redraw` 标志控制 OLED 帧缓冲区的刷新：

- **设为 1** 的时机：SPLASH 首次显示、切换到 MENU、按键操作、切换到 ENTERED
- **清 0** 的时机：每次 `draw_xxx()` 绘制完成
- 无操作时 `Menu_Task` 仅执行按键采样和状态判断，不做任何 I2C 通信

### 全屏刷新耗时

一次 `OLED_Update()` 通过软件 I2C 发送 1024 字节，耗时约 **34ms**。仅在按键按下时触发，日常轮询无开销。

### 三态状态机

```
          Menu_Init()
              │
              ▼
      ┌───────────────┐
      │  SPLASH       │  ── 显示 "HELLO" 500ms
      │  (500ms)      │
      └───────┬───────┘
              │ tick_ms - splash_start ≥ 500
              ▼
      ┌───────────────┐
      │  MENU         │  ── 按键导航 + 滚动列表
      │              │
      └───────┬───────┘
              │ 按 K3（确定）
              ▼
      ┌───────────────┐
      │  ENTERED      │  ── 显示项目名（占位）
      │  (待扩展)      │
      └───────────────┘
```

---

## 依赖关系

```
Menu_Task ─┬─ OLED_ShowChar / OLED_ShowString / OLED_Clear / OLED_Update
           ├─ Key_IsPressed(KEY_K1|K2|K3)
           └─ systick_get_ms() / tick_ms
```
