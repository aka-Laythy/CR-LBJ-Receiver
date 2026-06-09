# OLED 菜单 + 按键联调记录

## 环境
- MCU: CH32V005F6P6, HSI+PLL → 48MHz
- OLED: SSD1309 (SSD1306 驱动兼容), 128×64, 软件 I2C (PC2=SCL, PC1=SDA)
- 按键: K1(PD4,上), K2(PD0,下), K3(PC0,确定), GPIO_Mode_IPU + 100nF 硬件消抖
- 调度: 协作式, Menu_Task 10ms 周期

## 已解决问题

### 1. OLED 不亮
- **SysTick 死锁**: OLED 的 `Delay_Us()` 与 Tick 的 SysTick 中断争用硬件, `Delay_Us()` 最后关闭 SysTick 计数器, 中断处理函数又清除其等待的 SR 标志 → 死锁
- **解决**: `Delay_Us(1)` → `Tick_DelayUs(1)` (基于 NOP 空转, 不碰 SysTick)
- **I2C 引脚反接**: 代码 SCL=PC1,SDA=PC2, 硬件 SCL=PC2,SDA=PC1 → 信号交叉
- **解决**: 交换 `OLED_W_SCL`/`OLED_W_SDA` 的 GPIO_Pin

### 2. I2C 地址不匹配
- 误将 `addrSuffix` 改为 `0x02` (地址变 0x7A)
- **解决**: 恢复 `addrSuffix 0x00` (地址 0x78)

### 3. K3 进入内页后显示没更新
- `handle_keys()` 在 MENU 分支中将 `state=ENTERED, needs_redraw=1`, 但 MENU 分支紧接着调用 `draw_menu_list()` 把 `needs_redraw` 清 0
- ENTERED 分支永远收不到请求 → 内页不显示
- **解决**: MENU 分支加 `if (state==ENTERED) break;`

### 4. 内页中 K1/K2 可操作
- `handle_keys()` 未检查当前状态, 内页中也响应了方向键
- **解决**: K1/K2 逻辑包裹在 `if (state == MENU_STATE_MENU)` 内

### 5. K3 长按回主菜单后立刻又进入
- 内页长按 K3 → `state=MENU, needs_redraw=1`(此时 K3 还按着)
- 松手 → `!k3 && prev_k3` 被 MENU 分支捕获 → 再次进入
- **解决**: 引入 `k3_suppress_release` 标志. 长按回菜单时置 1, 松手时若标志为 1 则忽略此次释放. 下次 K3 新按压的上升沿清除标志.

### 6. ENTERED→MENU 后屏幕残留内页内容
- ENTERED 分支中 `handle_keys()` 切回 MENU 并设 `needs_redraw=1`, 但 ENTERED 分支继续执行 `draw_entered_item()` 把 `needs_redraw` 清 0
- 下次 MENU 分支看到 0 不重绘 → 菜单实际没画出来
- **解决**: ENTERED 分支的绘制条件加 `&& menu_state == MENU_STATE_ENTERED`

## K3 状态机

```
MENU 状态:
  K3 按下 → 记录时间, 清除 suppress 标志
  K3 松手 → 进入内页 (不检查时长)

ENTERED 状态:
  K3 按下 → 记录时间
  K3 按住 ≥600ms → 回主菜单, suppress=1
  K3 松手时 suppress=1 → 忽略 (不再次进入)
```

## 水平滚动设计
- 选中项文字 >120px(15字符×8px) 时触发
- 第 0~7 列(`>`区)始终由 `ClearArea` + 重画 `>` 保护
- 滚动参数: 2px/30ms, 两端暂停 1.2s
- 每次切到新项时 scroll_x 归零

## 剩余边界情况
- 内页中长时间按住 K1 后切回菜单, 自动连发可能立即触发 (极小概率)
- OLED_Update() 耗时 ~34ms, 此期间调度器阻塞, 按键采样延迟后移到下一 tick (边缘检测不受影响)
