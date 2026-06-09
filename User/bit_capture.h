#ifndef __BIT_CAPTURE_H
#define __BIT_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  Bit Capture 模块
 *  负责 PA1(DCLK)/PA2(DATA) 的 GPIO+EXTI 配置，
 *  在中断中逐 bit 接收并组装为 byte，提供环形缓冲区。
 * ================================================================ */

void     bit_capture_init(void);
void     bit_capture_isr(void);        /* 由 EXTI7_0_IRQHandler 调用 */
uint16_t bit_capture_available(void);  /* 缓冲区中可读的完整字节数 */
uint8_t  bit_capture_get_byte(void);   /* 取出一个字节（需先检查 available） */
void     bit_capture_reset(void);      /* 清空缓冲区 */
uint32_t bit_capture_irq_count(void);  /* 调试: EXTI 中断触发次数 */
uint16_t bit_capture_overflow(void);   /* 调试: 缓冲区溢出丢弃字节数 */

#ifdef __cplusplus
}
#endif

#endif /* __BIT_CAPTURE_H */
