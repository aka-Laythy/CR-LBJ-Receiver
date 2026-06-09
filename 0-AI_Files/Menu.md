# Menu 模块

## 文件位置

```
User/OLED/
├── Menu.h          # 公开 API
├── Menu.c          # 实现
```

依赖：`OLED.h/c`、`OLED_Data.h/c`、`Key.h/c`、`Tick.h/c`、`GPS.h/c`、`SX1276/sx1276.h`

---

## API

```c
void Menu_Init(void);
void Menu_Task(void);
```

`Menu_Init` 在调度器启动前调用一次，`Menu_Task` 注册为调度任务（周期 ≤20ms）。

---

## 集成

```c
// main.c
#include "OLED/Menu.h"

Key_Init();
Menu_Init();

schedule_init(10);
schedule_register(20, schedule_task1);
schedule_register(10, Menu_Task);
```

---

## 菜单项（9 项，序号动态生成）

| # | 名称 | 内页 |
|---|------|------|
| 1 | System Status | 系统状态 |
| 2 | Freq Settings | 占位 |
| 3 | Signal Detection | 占位 |
| 4 | Data Recording | 占位 |
| 5 | SX1276 Debug | 寄存器调试 |
| 6 | GPS Debug | 经纬度度分秒 + 日期时间 |
| 7 | Network Config | 占位 |
| 8 | Storage Mgmt | 占位 |
| 9 | Power Off / Reboot | 占位 |

---

## 按键

| 按键 | 主菜单 | 内页 |
|------|--------|------|
| K1（上） | 上一项，长按连发 | 内容上滚 |
| K2（下） | 下一项，长按连发 | 内容下滚 |
| K3 短按 | 进入内页 | — |
| K3 长按 | — | 返回主菜单 |

长按 K3 返回后 `k3_suppress_release` 标志阻止松手再次触发进入。

---

## 水平滚动

选中行文字 > 120px 时自动左右滚动。

- 速度：2px / 30ms，两端暂停 1.2s
- `max_x = tw - max_w + 2`（右侧补偿）
- 滚动时 `tx = TEXT_X - sx + (sx > 0 ? 2 : 0)` 保留间隙
- 第 0~6 列由 `ClearArea(0, 7, 16)` + 重画 `>` 保护

---

## 内页

`page_callbacks[]` 函数指针数组驱动。每 1 秒自动刷新。

**System Status（2 行）：**
```
FW0001
06/09 13:30:45 +     ← MM/DD HH:MM:SS, +/- Fix
```

**SX1276 Debug（4 行）：**
```
RSSI:-119
OP:05 V:12 LN:20
F:CD4F33 B:682B
FD:4A BW:14 P:AA
```

**GPS Debug（4 行）：**
```
Lon:103*35'28"E
Lat:30*34'05"N
Date:2026/06/09
Time:13:30:45 +     ← +/- Fix
```

---

## GPS 解析

`GPS_ParseNMEA` 先 RMC（时间+日期）后 GGA（定位质量）。`parse_RMC` 始终返回 true，`data->valid` 保持 Fix 状态。

---

## 状态机

```
SPLASH (500ms) → LIST (主菜单) ↔ PAGE (内页, K3 long back)
```
