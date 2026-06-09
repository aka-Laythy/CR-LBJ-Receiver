#include <ch32v00X.h>
#include "spi_bus.h"

/*----------------------------------------------------------------------------
 * SPI1 默认引脚（无需重映射）：
 *   PC5 = SPI1_SCK
 *   PC6 = SPI1_MOSI
 *   PC7 = SPI1_MISO
 *
 * SX1276 CS = PC4  (普通 GPIO)
 * W25Q64 CS = PC3  (普通 GPIO)
 *----------------------------------------------------------------------------*/

#define SPI_PERIPH       SPI1
#define SPI_BUS_CLK_EN() RCC_PB2PeriphClockCmd(RCC_PB2Periph_SPI1 | \
                                                RCC_PB2Periph_GPIOC | \
                                                RCC_PB2Periph_AFIO, ENABLE)

#define CS_SX1276_PORT   GPIOC
#define CS_SX1276_PIN    GPIO_Pin_4

#define CS_W25Q64_PORT   GPIOC
#define CS_W25Q64_PIN    GPIO_Pin_3

static SPI_CS_Device_t current_cs = SPI_CS_NONE;

static void cs_high(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_WriteBit(port, pin, Bit_SET);
}

static void cs_low(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_WriteBit(port, pin, Bit_RESET);
}

static void CS_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* SX1276 CS: PC4 — 默认高电平 */
    GPIO_InitStructure.GPIO_Pin   = CS_SX1276_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(CS_SX1276_PORT, &GPIO_InitStructure);
    cs_high(CS_SX1276_PORT, CS_SX1276_PIN);

    /* W25Q64 CS: PC3 — 默认高电平 */
    GPIO_InitStructure.GPIO_Pin   = CS_W25Q64_PIN;
    GPIO_Init(CS_W25Q64_PORT, &GPIO_InitStructure);
    cs_high(CS_W25Q64_PORT, CS_W25Q64_PIN);
}

static void SPI_AF_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /*
     * CH32V005F6P6 的 SPI1 默认引脚就是 PC5(SCK) / PC6(MOSI) / PC7(MISO)
     * 无需 AFIO 重映射，直接复用
     */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

void SPI_Bus_Init(void)
{
    SPI_InitTypeDef SPI_InitStructure = {0};

    SPI_BUS_CLK_EN();    /* SPI1 + GPIOC + AFIO */
    CS_GPIO_Init();      /* PC4(SX1276), PC3(W25Q64) 输出高 */
    SPI_AF_Init();       /* PC5/PC6/PC7 AF 复用（默认位置） */

    /* SPI 配置：主模式、8bit、CPOL=Low、CPHA=1Edge、软件 NSS、MSB 优先 */
    SPI_NSSInternalSoftwareConfig(SPI_PERIPH, SPI_NSSInternalSoft_Reset);
    SPI_SSOutputCmd(SPI_PERIPH, DISABLE);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;  /* PCLK/8 */
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;

    SPI_Init(SPI_PERIPH, &SPI_InitStructure);
    SPI_Cmd(SPI_PERIPH, ENABLE);
}

void SPI_Bus_Select(SPI_CS_Device_t dev)
{
    GPIO_TypeDef *port;
    uint16_t pin;

    if (current_cs == dev) {
        return;
    }
    if (current_cs != SPI_CS_NONE) {
        SPI_Bus_Release();
    }
    switch (dev) {
        case SPI_CS_SX1276:
            port = CS_SX1276_PORT;
            pin  = CS_SX1276_PIN;
            break;
        case SPI_CS_W25Q64:
            port = CS_W25Q64_PORT;
            pin  = CS_W25Q64_PIN;
            break;
        default:
            current_cs = SPI_CS_NONE;
            return;
    }
    cs_low(port, pin);
    current_cs = dev;
}

void SPI_Bus_Release(void)
{
    if (current_cs == SPI_CS_NONE) {
        return;
    }
    switch (current_cs) {
        case SPI_CS_SX1276:
            cs_high(CS_SX1276_PORT, CS_SX1276_PIN);
            break;
        case SPI_CS_W25Q64:
            cs_high(CS_W25Q64_PORT, CS_W25Q64_PIN);
            break;
        default:
            return;
    }
    current_cs = SPI_CS_NONE;
}

uint8_t SPI_Bus_TransferByte(uint8_t tx)
{
    while (SPI_I2S_GetFlagStatus(SPI_PERIPH, SPI_I2S_FLAG_TXE) == RESET) {
    }
    SPI_I2S_SendData(SPI_PERIPH, tx);
    while (SPI_I2S_GetFlagStatus(SPI_PERIPH, SPI_I2S_FLAG_RXNE) == RESET) {
    }
    return (uint8_t)SPI_I2S_ReceiveData(SPI_PERIPH);
}

void SPI_Bus_Transfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint8_t r = SPI_Bus_TransferByte(tx ? tx[i] : 0xFF);
        if (rx) rx[i] = r;
    }
}

void SPI_Bus_Write(uint8_t *data, uint16_t len)
{
    SPI_Bus_Transfer(data, NULL, len);
}

void SPI_Bus_Read(uint8_t *buf, uint16_t len)
{
    SPI_Bus_Transfer(NULL, buf, len);
}
