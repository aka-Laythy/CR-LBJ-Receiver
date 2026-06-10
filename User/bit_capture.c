#include "bit_capture.h"
#include <ch32v00X.h>

/* ================================================================
 *  DIO 引脚定义
 *  PA1 = DIO1/DCLK (EXTI1, 上升沿中断)
 *  PA2 = DIO2/DATA (普通输入, ISR 中读取)
 * ================================================================ */
#define DCLK_PORT   GPIOA
#define DCLK_PIN    GPIO_Pin_1
#define DATA_PORT   GPIOA
#define DATA_PIN    GPIO_Pin_2

/* ================================================================
 *  环形字节缓冲区 (16 字节, 对应约 106ms 的 1200bps 数据)
 * ================================================================ */
#define BUF_SIZE 128U
#define BUF_MASK (BUF_SIZE - 1U)

static volatile uint8_t  buf[BUF_SIZE];
static volatile uint16_t wr_idx;    /* ISR 写入位置 */
static volatile uint16_t rd_idx;    /* ISR(溢出时) / 主循环读出位置 */

/* 调试计数器 */
static volatile uint32_t g_irq_count;
static volatile uint16_t g_overflow_bytes;

/* ================================================================
 *  ISR 内 bit 累积状态
 * ================================================================ */
static volatile uint8_t  cur_byte;
static volatile uint8_t  bit_count;

/* ================================================================
 *  初始化
 * ================================================================ */
void bit_capture_init(void)
{
    GPIO_InitTypeDef  g = {0};
    EXTI_InitTypeDef  e = {0};
    NVIC_InitTypeDef  n = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA | RCC_PB2Periph_AFIO, ENABLE);

    /* PA1: DCLK 浮空输入 */
    g.GPIO_Pin  = DCLK_PIN;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(DCLK_PORT, &g);

    /* PA2: DATA 浮空输入 */
    g.GPIO_Pin  = DATA_PIN;
    GPIO_Init(DATA_PORT, &g);

    /* PA1 → EXTI_Line1 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    /* EXTI1: 上升沿中断 (DATA 在 DCLK 上升沿有效) */
    e.EXTI_Line    = EXTI_Line1;
    e.EXTI_Mode    = EXTI_Mode_Interrupt;
    e.EXTI_Trigger = EXTI_Trigger_Rising;
    e.EXTI_LineCmd = ENABLE;
    EXTI_Init(&e);

    /* NVIC: EXTI7_0 IRQ, 抢占优先级 1 */
    n.NVIC_IRQChannel                   = EXTI7_0_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority        = 0;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);

    bit_capture_reset();
}

/* ================================================================
 *  ISR 入口
 *  由 EXTI7_0_IRQHandler 调用。
 *  累加 bit，每 8 个 bit 组装为一个 byte 写入环形缓冲区。
 * ================================================================ */
void bit_capture_isr(void)
{
    uint8_t bit;

    bit = (GPIO_ReadInputDataBit(DATA_PORT, DATA_PIN) != Bit_RESET) ? 1 : 0;

    /* MSB-first: 先收到的 bit 是字节高位 */
    cur_byte = (cur_byte << 1) | bit;
    bit_count++;

    if (bit_count >= 8) {
        uint16_t w = wr_idx;
        if ((uint16_t)(w - rd_idx) >= BUF_SIZE) {
            rd_idx++;
            g_overflow_bytes++;
        }
        buf[w & BUF_MASK] = cur_byte;
        wr_idx = w + 1;
        cur_byte  = 0;
        bit_count = 0;
    }
    g_irq_count++;
}

/* ================================================================
 *  缓冲区内完整字节数
 * ================================================================ */
uint16_t bit_capture_available(void)
{
    uint16_t w = wr_idx;  /* 单次 volatile 读取，避免竞态 */
    return (uint16_t)(w - rd_idx);
}

/* ================================================================
 *  取出一个字节
 * ================================================================ */
uint8_t bit_capture_get_byte(void)
{
    uint8_t byte = buf[rd_idx & BUF_MASK];
    rd_idx++;
    return byte;
}

/* ================================================================
 *  清空缓冲区
 * ================================================================ */
void bit_capture_reset(void)
{
    wr_idx    = 0;
    rd_idx    = 0;
    cur_byte  = 0;
    bit_count = 0;
}

uint32_t bit_capture_irq_count(void)
{
    return g_irq_count;
}

uint16_t bit_capture_overflow(void)
{
    return g_overflow_bytes;
}
