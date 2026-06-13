#ifndef __SPI_BUS_H
#define __SPI_BUS_H

#include <ch32v00X.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI_CS_SX1276 = 0,
    SPI_CS_W25Q64 = 1,
    SPI_CS_NONE   = 2
} SPI_CS_Device_t;

void SPI_Bus_Init(void);
void SPI_Bus_Select(SPI_CS_Device_t dev);
void SPI_Bus_Release(void);
uint8_t SPI_Bus_TransferByte(uint8_t tx);
void SPI_Bus_Transfer(uint8_t *tx, uint8_t *rx, uint16_t len);
void SPI_Bus_Write(uint8_t *data, uint16_t len);
void SPI_Bus_Read(uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
