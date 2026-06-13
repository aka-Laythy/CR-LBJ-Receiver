#ifndef __TICK_H
#define __TICK_H

#include "debug.h"

/*=============================================================================
 * SysTick 全局嘀嗒时钟驱动
 * 时钟源：HCLK（48MHz）
 * 中断周期：1ms
 * @note 启用本模块后，debug.c 中的 Delay_Ms() / Delay_Us() 将因 SR 标志
 *       被中断清除而死锁，请统一改用 Tick_DelayMs() / Tick_DelayUs()。
 *===========================================================================*/

extern volatile uint32_t tick_ms;

void Tick_Init(void);
void Tick_DelayMs(uint32_t ms);
void Tick_DelayUs(uint32_t us);

/*=============================================================================
 * @brief   获取当前系统滴答计数（ms）
 * @return  uint32_t 当前 tick_ms 值
 * @note    此函数为内联函数，无调用开销。
 *          tick_ms 为 uint32_t，约 49.7 天溢出一次。
 *          使用无符号减法 (now - last) 可安全处理溢出回绕。
 *===========================================================================*/
static inline uint32_t systick_get_ms(void)
{
    return tick_ms;
}

#endif /* __TICK_H */
