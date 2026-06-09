#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "debug.h"

#define SCHEDULE_MAX_TASKS  8

typedef void (*schedule_callback_t)(void);

/* 初始化调度器，并创建第 0 号任务（使用 schedule_on_interval 作为用户入口） */
void schedule_init(uint32_t interval_ms);

/* 注册一个新的周期性任务，返回任务 ID (1~7)，失败返回 -1 */
int schedule_register(uint32_t interval_ms, schedule_callback_t callback);

/* 使能/禁能某个任务 */
void schedule_enable(int task_id, uint8_t enable);

/* 动态修改某个任务的间隔 */
void schedule_set_interval(int task_id, uint32_t interval_ms);

/* 轮询所有任务，请放到 main loop 中尽可能频繁调用 */
void schedule_poll(void);

/* ---------- 用户代码入口 ----------
 * 如果你只使用单任务（task 0），在任意 .c 文件中定义下面的函数即可。
 * 例如：
 *     void schedule_on_interval(void)
 *     {
 *         gpio_bit_toggle(LED_GPIO, LED_PIN);
 *     }
 */
/* ARM Compiler 5 (keil AC5) uses __weak; ARM Compiler 6 / GCC use __attribute__((weak)) */
#if defined (__CC_ARM)
    #define SCHEDULE_WEAK __weak
#else
    #define SCHEDULE_WEAK __attribute__((weak))
#endif

SCHEDULE_WEAK void schedule_task0(void);

#endif /* SCHEDULE_H */
