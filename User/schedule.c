#include "debug.h"
#include "schedule.h"

typedef struct {
    uint32_t interval_ms;
    uint32_t last_ms;
    schedule_callback_t callback;
    uint8_t  enable;
} schedule_task_t;

static schedule_task_t tasks[SCHEDULE_MAX_TASKS];
static int task_count = 0;

/**
 * @brief 初始化调度器
 * @param interval_ms: 第 0 号任务的执行间隔（单位：ms）
 * @note  会自动清零所有任务槽，并把 schedule_on_interval 注册为 task0 的回调
 */
void schedule_init(uint32_t interval_ms)
{
    int i;

    task_count = 0;
    for (i = 0; i < SCHEDULE_MAX_TASKS; i++) {
        tasks[i].interval_ms = 0;
        tasks[i].last_ms     = 0;
        tasks[i].callback    = 0;
        tasks[i].enable      = 0;
    }

    /* 注册 task 0（legacy 单任务接口） */
    tasks[0].interval_ms = interval_ms;
    tasks[0].last_ms     = systick_get_ms();
    tasks[0].callback    = schedule_task0;
    tasks[0].enable      = 1;
    task_count = 1;
}

/**
 * @brief 注册新任务
 * @param interval_ms: 执行间隔（ms），设为 0 表示该任务不执行
 * @param callback:    任务回调函数指针
 * @retval 任务 ID (>=1)，或 -1 表示任务槽已满
 */
int schedule_register(uint32_t interval_ms, schedule_callback_t callback)
{
    int id;

    if (task_count >= SCHEDULE_MAX_TASKS) {
        return -1;
    }

    id = task_count++;
    tasks[id].interval_ms = interval_ms;
    tasks[id].last_ms     = systick_get_ms();
    tasks[id].callback    = callback;
    tasks[id].enable      = 1;

    return id;
}

/**
 * @brief 使能或禁能某个任务
 */
void schedule_enable(int task_id, uint8_t enable)
{
    if (task_id >= 0 && task_id < task_count) {
        tasks[task_id].enable = enable;
    }
}

/**
 * @brief 动态修改某个任务的间隔
 */
void schedule_set_interval(int task_id, uint32_t interval_ms)
{
    if (task_id >= 0 && task_id < task_count) {
        tasks[task_id].interval_ms = interval_ms;
    }
}

/**
 * @brief 轮询所有任务
 * @note  应放在 main loop 中尽可能频繁调用
 *
 * 核心原理：
 *   1. 使用 tick_ms（不是 tick_ms_count！）作为时间基准。
 *   2. 用无符号减法 (now - last) >= interval 判断是否超时。
 *      uint32_t 溢出后会自动回绕，只要两次调用的真实间隔 < 2^32 ms（约 49.7 天），
 *      差值就一定正确。因此不存在溢出归零导致的漏判问题。
 *   3. "防漏检"：即使主循环某次卡了很久，只要发现 elapsed >= interval，
 *      就会执行一次，不会漏掉。
 *   4. "防突发"：如果卡了非常久（超过 2 个周期），为了防止恢复后连续狂触发，
 *      直接将 last_ms 对齐到 now，只执行一次。若你需要严格固定频率补回所有
 *       missed 的任务，可把下面的 if-else 改成 last_ms += interval_ms。
 */
void schedule_poll(void)
{
    uint32_t now = systick_get_ms();
    int i;

    for (i = 0; i < task_count; i++) {
        if (!tasks[i].enable || tasks[i].interval_ms == 0) {
            continue;
        }

        uint32_t elapsed = now - tasks[i].last_ms;

        if (elapsed >= tasks[i].interval_ms) {
            /* 若阻塞超过 2 个周期，重置基准，防止连续爆触发 */
            if (elapsed >= (2 * tasks[i].interval_ms)) {
                tasks[i].last_ms = now;
            } else {
                tasks[i].last_ms += tasks[i].interval_ms;
            }

            if (tasks[i].callback != 0) {
                tasks[i].callback();
            }
        }
    }
}

/**
 * @brief 默认的用户代码入口（weak）
 * @note  如果你使用单任务模式（schedule_init），请在别的 .c 文件中
 *        重新实现这个函数。多任务模式下请直接使用 schedule_register 注册回调。
 */
SCHEDULE_WEAK void schedule_task0(void)
{
    /* User code entry: add periodic task code here. */
}
