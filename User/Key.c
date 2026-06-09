#include "Key.h"

/*=============================================================================
 * @brief   按键 GPIO 初始化
 * @note    K1(PD4), K2(PD0), K3(PC0) 配置为上拉输入
 *===========================================================================*/
void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4 | GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*=============================================================================
 * @brief   查询按键状态
 * @param   key : 按键 ID (KEY_K1 / KEY_K2 / KEY_K3)
 * @return  1 = 按下（GPIO 低电平）, 0 = 释放（GPIO 高电平）
 *===========================================================================*/
uint8_t Key_IsPressed(Key_ID_t key)
{
    switch (key) {
        case KEY_K1: return (GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_4) == Bit_RESET) ? 1 : 0;
        case KEY_K2: return (GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_0) == Bit_RESET) ? 1 : 0;
        case KEY_K3: return (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_0) == Bit_RESET) ? 1 : 0;
        default:     return 0;
    }
}
