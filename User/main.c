#include "debug.h"
#include "main.h"
#include "Tick.h"
#include "BLE.h"
#include "GPS.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    __enable_irq();

    Tick_Init();            // SysTick 1ms 全局嘀嗒时钟
    GPS_Init();             // USART1 — DX-GP21 GNSS 模组
    BLE_Init();             // USART2_AF3 — DX-BT311 蓝牙模组

    while(1)
    {
        ;
    }
}
