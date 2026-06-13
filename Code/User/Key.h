#ifndef __KEY_H
#define __KEY_H

#include "debug.h"

/*=============================================================================
 * 按键模块驱动
 * K1-K3 轻触按键，上拉输入模式
 * 外部 100nF 硬件消抖，另一侧接地
 * 读取到 =0 代表按下
 * 
 *   K1 — PD4
 *   K2 — PD0
 *   K3 — PC0
 *===========================================================================*/

typedef enum {
    KEY_K1 = 0,
    KEY_K2 = 1,
    KEY_K3 = 2
} Key_ID_t;

void     Key_Init(void);
uint8_t  Key_IsPressed(Key_ID_t key);     /* return 1=pressed, 0=released */

#endif /* __KEY_H */
