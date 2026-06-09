# Schedule 模块

基于 SysTick `tick_ms` 的轻量级轮询调度器，支持单任务 / 多任务。不依赖中断，在 `main()` 循环里调用 `schedule_poll()` 即可。

## 核心原理

- 时间基准：`systick_get_ms()` 返回的 **`tick_ms`**（**不要**用每 1000 ms 归零的 `tick_ms_count`）。
- 溢出安全：`tick_ms` 是 `uint32_t`，约 49.7 天溢出。用**无符号减法** `(now - last) >= interval` 判断，溢出自动回绕，不会漏检。
- 防突发：若主循环卡死很久（超过 2 个周期），恢复后只执行一次，避免连续狂触发。

## 单任务模式（最简单）

在任意 `.c` 文件里实现 `schedule_on_interval()`：

```c
#include "Schedule/schedule.h"

void schedule_on_interval(void)
{
    gpio_bit_toggle(LED_GPIO, LED_PIN);   /* 用户周期性代码 */
}
```

在 `main()` 里初始化和轮询：

```c
schedule_init(100);          /* 每 100 ms 执行一次 schedule_on_interval */

while (1) {
    schedule_poll();         /* 尽可能频繁调用 */
}
```

## 多任务模式（推荐）

注册回调，不用重写弱定义函数：

```c
#include "Schedule/schedule.h"

void task_100ms(void) { /* ... */ }
void task_500ms(void) { /* ... */ }

int main(void)
{
    schedule_init(100);                        /* task 0 */
    schedule_register(500, task_500ms);        /* task 1 */

    while (1) {
        schedule_poll();
    }
}
```

## API

| 函数 | 说明 |
|------|------|
| `schedule_init(interval_ms)` | 初始化调度器，并创建 task 0 |
| `schedule_register(interval_ms, callback)` | 注册新任务，返回任务 ID（`-1` 表示满） |
| `schedule_enable(id, enable)` | 使能 / 禁能任务 |
| `schedule_set_interval(id, interval_ms)` | 动态修改任务间隔 |
| `schedule_poll()` | 轮询所有任务，放主循环 |

## 注意事项

1. `schedule_poll()` 调用越频繁，定时精度越高；如果有 `delay_1ms()` 阻塞太久，会影响调度。
2. 若需要**严格固定频率补回**所有 missed 的任务，把 `schedule.c` 里 `schedule_poll()` 的防突发 `if-else` 改成：
   ```c
   tasks[i].last_ms += tasks[i].interval_ms;
   ```
