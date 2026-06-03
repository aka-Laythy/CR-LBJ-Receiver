#ifndef __USART_H
#define __USART_H

#include "debug.h"

void USART1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOC | RCC_PB2Periph_USART1 | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap3_USART1, ENABLE);
    /* USART1_3 TX-->C.0   RX-->C.1 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

void USART2_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_USART2 | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap3_USART2, ENABLE);
    /* USART2_3 TX-->D.2   RX-->D.3 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;           // USART2中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;   // 抢占优先级1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;          // 子优先级1
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

void BLE_Init(void)
{
    USART2_Init();
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio);
    GPIO_WriteBit(GPIOC, GPIO_Pin_5, Bit_RESET);
}

void USART2_SendString(uint8_t *str)
{
    uint8_t i;
    while(g_usart2_tx_busy);    //等待上一次发送完成（阻塞式等待，也可改为返回0/1表示是否成功）
    for(i = 0; i < 19; i++)     // 复制字符串到发送缓冲区（最多19字节，保留1字节给\0）
    {
        if(str[i] == '\0') break;
        g_usart2_tx_buf[i] = str[i];
    }
    g_usart2_tx_len = i;        // 实际发送长度（不含\0）
    g_usart2_tx_idx = 0;        // 索引归零
    g_usart2_tx_busy = 1;       // 标记发送忙
    if(g_usart2_tx_len > 0)     // 先发送第一个字节，剩余字节在TXE中断中发送
    {
        USART_SendData(USART2, g_usart2_tx_buf[0]);
        g_usart2_tx_idx = 1;
        USART_ITConfig(USART2, USART_IT_TXE, ENABLE);  // 开启发送空中断
    }
    else
    {
        g_usart2_tx_busy = 0;   // 空字符串直接置闲
    }
}

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)    // ----- 接收中断（RXNE） -----
    {
        g_usart2_rx_byte = USART_ReceiveData(USART2);        // 读取数据（自动清RXNE标志）
        g_usart2_rx_flag = 1;                                // 置位新数据标志
    }
    if(USART_GetITStatus(USART2, USART_IT_TXE) != RESET)     // ----- 发送中断（TXE） -----
    {
        if(g_usart2_tx_idx < g_usart2_tx_len)
        {
            USART_SendData(USART2, g_usart2_tx_buf[g_usart2_tx_idx]);   // 写DR自动清除TXE标志
            g_usart2_tx_idx++;
        }
        else
        {

            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);              // 发送完成，关闭TXE中断，置闲
            g_usart2_tx_busy = 0;
        }

    }
}

#endif /* __USART_H */
