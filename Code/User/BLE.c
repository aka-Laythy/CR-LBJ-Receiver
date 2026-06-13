#include "BLE.h"
#include "Tick.h"

/*=============================================================================
 * 环形缓冲区数据结构
 *===========================================================================*/
typedef struct {
    volatile uint8_t  buf[BLE_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
} RingBuf_t;

static volatile RingBuf_t ble_rx_ring = {0};
static volatile RingBuf_t ble_tx_ring = {0};
static volatile uint8_t   ble_tx_busy = 0;

/*=============================================================================
 * 内部静态函数：环形缓冲区操作
 *===========================================================================*/
static inline uint8_t __ring_push(volatile RingBuf_t *r, uint8_t byte, uint16_t size)
{
    if (r->count >= size) {
        return 0;   /* 缓冲区满 */
    }
    r->buf[r->head] = byte;
    r->head = (r->head + 1) % size;
    r->count++;
    return 1;
}

static inline uint8_t __ring_pop(volatile RingBuf_t *r, uint8_t *byte, uint16_t size)
{
    if (r->count == 0) {
        return 0;   /* 缓冲区空 */
    }
    *byte = r->buf[r->tail];
    r->tail = (r->tail + 1) % size;
    r->count--;
    return 1;
}

/*=============================================================================
 * @brief   BLE / USART2 初始化
 * @note    1. 配置 PD2(TX)、PD3(RX) 为 USART2 复用功能
 *          2. 先以 115200 初始化，等待模组就绪
 *          3. 发送 AT+UART=8\r\n 切换到 921600
 *          4. 将 USART2 重设为 921600
 *===========================================================================*/
static void _uart_send_byte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, byte);
}

void BLE_Init(void)
{
    struct { GPIO_InitTypeDef g; USART_InitTypeDef u; NVIC_InitTypeDef n; } i = {0};
    i.u.USART_BaudRate = 115200;
    i.u.USART_WordLength          = USART_WordLength_8b;
    i.u.USART_StopBits            = USART_StopBits_1;
    i.u.USART_Parity              = USART_Parity_No;
    i.u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    i.u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_USART2 | RCC_PB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap3_USART2, ENABLE);

    i.g.GPIO_Pin = GPIO_Pin_2; i.g.GPIO_Speed = GPIO_Speed_30MHz; i.g.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &i.g);

    i.g.GPIO_Pin = GPIO_Pin_3; i.g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &i.g);

    USART_Init(USART2, &i.u);

    i.n.NVIC_IRQChannel                   = USART2_IRQn;
    i.n.NVIC_IRQChannelPreemptionPriority = 1;
    i.n.NVIC_IRQChannelSubPriority        = 2;
    i.n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&i.n);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    /* 等待模组上电就绪（+READY\r\n）后发送 AT+UART=8 切换至 921600 */
    Tick_DelayMs(500);
    _uart_send_byte('A'); _uart_send_byte('T');
    _uart_send_byte('+'); _uart_send_byte('U'); _uart_send_byte('A'); _uart_send_byte('R'); _uart_send_byte('T');
    _uart_send_byte('='); _uart_send_byte('8');
    _uart_send_byte('\r'); _uart_send_byte('\n');
    Tick_DelayMs(100);

    i.u.USART_BaudRate = 921600;
    USART_Init(USART2, &i.u);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
}

/*=============================================================================
 * @brief   发送单字节（非阻塞）
 * @return  1=成功写入发送缓冲区，0=发送缓冲区满
 *===========================================================================*/
uint8_t BLE_SendByte(uint8_t byte)
{
    uint8_t ret;

    __disable_irq();
    ret = __ring_push(&ble_tx_ring, byte, BLE_TX_BUF_SIZE);

    if (ret && !ble_tx_busy) {
        ble_tx_busy = 1;
        USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
    }
    __enable_irq();

    return ret;
}

/*=============================================================================
 * @brief   发送数据块（非阻塞）
 * @return  1=全部写入成功，0=中途缓冲区满（已发送部分数据）
 *===========================================================================*/
uint8_t BLE_SendData(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (!BLE_SendByte(data[i])) {
            return 0;
        }
    }
    return 1;
}

/*=============================================================================
 * @brief   发送以 '\0' 结尾的字符串（非阻塞）
 * @return  1=全部写入成功，0=中途缓冲区满
 *===========================================================================*/
uint8_t BLE_SendString(const uint8_t *str)
{
    while (*str) {
        if (!BLE_SendByte(*str++)) {
            return 0;
        }
    }
    return 1;
}

/*=============================================================================
 * @brief   从接收缓冲区读取单字节
 * @return  1=读取成功，0=接收缓冲区空
 *===========================================================================*/
uint8_t BLE_ReadByte(uint8_t *byte)
{
    uint8_t ret;
    __disable_irq();
    ret = __ring_pop(&ble_rx_ring, byte, BLE_RX_BUF_SIZE);
    __enable_irq();
    return ret;
}

/*=============================================================================
 * @brief   从接收缓冲区批量读取
 * @return  实际读取到的字节数
 *===========================================================================*/
uint16_t BLE_ReadData(uint8_t *buf, uint16_t len)
{
    uint16_t i = 0;
    __disable_irq();
    while (i < len && __ring_pop(&ble_rx_ring, &buf[i], BLE_RX_BUF_SIZE)) {
        i++;
    }
    __enable_irq();
    return i;
}

/*=============================================================================
 * @brief   获取接收缓冲区当前已缓存字节数
 *===========================================================================*/
uint16_t BLE_GetRxCount(void)
{
    uint16_t ret;
    __disable_irq();
    ret = ble_rx_ring.count;
    __enable_irq();
    return ret;
}

/*=============================================================================
 * @brief   清空接收缓冲区
 *===========================================================================*/
void BLE_ClearRx(void)
{
    __disable_irq();
    ble_rx_ring.head  = 0;
    ble_rx_ring.tail  = 0;
    ble_rx_ring.count = 0;
    __enable_irq();
}

/*=============================================================================
 * @brief   查询发送是否忙
 * @return  1=正在发送，0=空闲
 *===========================================================================*/
uint8_t BLE_IsTxBusy(void)
{
    return ble_tx_busy;
}

/*=============================================================================
 * @brief   USART2 中断服务函数
 * @note    RXNE：接收数据写入环形缓冲区
 *          TXE ：从环形缓冲区取出数据发送，发完则关 TXE 中断
 *===========================================================================*/
void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    uint8_t byte;

    /* ----- 接收中断 ----- */
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        byte = (uint8_t)USART_ReceiveData(USART2);
        __ring_push(&ble_rx_ring, byte, BLE_RX_BUF_SIZE);
    }

    /* ----- 发送中断 ----- */
    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET) {
        if (__ring_pop(&ble_tx_ring, &byte, BLE_TX_BUF_SIZE)) {
            USART_SendData(USART2, byte);
        } else {
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
            ble_tx_busy = 0;
        }
    }
}
