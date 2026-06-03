#include "Tick.h"

volatile uint32_t tick_ms = 0;

/*=============================================================================
 * @brief   SysTick 初始化 — 配置为 1ms 周期中断
 * @note    时钟源 = HCLK，自动重载，使能中断
 *          CMP = SystemCoreClock / 1000 - 1 （48MHz 下为 47999）
 *===========================================================================*/
void Tick_Init(void)
{
    SysTick->SR  = 0;
    SysTick->CNT = 0;
    SysTick->CMP = SystemCoreClock / 1000 - 1;

    NVIC_EnableIRQ(SysTick_IRQn);

    /* CTLR 位定义：
     * bit0 (STK_STE)   = 1: 计数器使能
     * bit1 (STK_STIE)  = 1: 中断使能
     * bit2 (STK_STCLK) = 1: HCLK 时钟源
     * bit3 (STK_STRE)  = 1: 自动重载使能
     * 即 0x0F */
    SysTick->CTLR = 0x0F;
}

/*=============================================================================
 * @brief   SysTick 中断服务函数
 *===========================================================================*/
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void)
{
    tick_ms++;
    SysTick->SR = 0;   /* 必须清除标志，否则中断会持续触发 */
}

/*=============================================================================
 * @brief   毫秒级阻塞延迟
 * @note    基于全局 tick_ms，与 SysTick 中断兼容
 *===========================================================================*/
void Tick_DelayMs(uint32_t ms)
{
    uint32_t start = tick_ms;
    while ((tick_ms - start) < ms)
    {
        ;
    }
}

/*=============================================================================
 * @brief   微秒级阻塞延迟（粗略空转实现）
 * @note    不占用 SysTick，基于 __NOP 空转。
 *          若需更精确时序，建议改用硬件定时器捕获/比较。
 *===========================================================================*/
void Tick_DelayUs(uint32_t us)
{
    /* 按 48MHz HCLK 估算：约 48 个时钟周期 = 1us
     * 考虑循环体开销，取经验值 8 次 __NOP ≈ 1us（-Os 优化下） */
    uint32_t total = us * 8;
    while (total--)
    {
        __NOP();
    }
}
