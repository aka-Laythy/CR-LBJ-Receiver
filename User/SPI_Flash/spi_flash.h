#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* W25Q64 JEDEC ID = 0xEF, 0x40, 0x17 */
#define SPI_FLASH_JEDEC_MF          0xEF  /* Winbond */
#define SPI_FLASH_JEDEC_TYPE        0x40  /* W25Q64 */
#define SPI_FLASH_JEDEC_CAP         0x17  /* 64Mbit */

/* 容量 */
#define SPI_FLASH_PAGE_SIZE         256
#define SPI_FLASH_SECTOR_SIZE       4096
#define SPI_FLASH_BLOCK32_SIZE      32768
#define SPI_FLASH_BLOCK64_SIZE      65536
#define SPI_FLASH_TOTAL_SIZE        (8 * 1024 * 1024) /* 8MB */

/* 指令 */
#define CMD_WRITE_ENABLE           0x06
#define CMD_WRITE_DISABLE          0x04
#define CMD_READ_STATUS1           0x05
#define CMD_WRITE_STATUS1          0x01
#define CMD_READ_DATA              0x03
#define CMD_FAST_READ              0x0B
#define CMD_PAGE_PROGRAM           0x02
#define CMD_SECTOR_ERASE           0x20
#define CMD_BLOCK32_ERASE          0x52
#define CMD_BLOCK64_ERASE          0xD8
#define CMD_CHIP_ERASE             0xC7
#define CMD_POWER_DOWN             0xB9
#define CMD_RELEASE_PD_ID          0xAB
#define CMD_READ_JEDEC_ID          0x9F

/* 存储分区 */
#define FLASH_PART_CONFIG_ADDR     0x000000
#define FLASH_PART_CONFIG_SIZE     0x004000     /* 16 KB */

#define FLASH_PART_RESERVED_ADDR   0x004000
#define FLASH_PART_RESERVED_SIZE   0x100000     /* 1 MB */

#define FLASH_PART_FONT_ADDR       0x104000
#define FLASH_PART_FONT_SIZE       0x100000     /* 1 MB */

#define FLASH_PART_LBJ_ADDR        0x204000
#define FLASH_PART_LBJ_SIZE        (SPI_FLASH_TOTAL_SIZE - FLASH_PART_LBJ_ADDR)  /* ~5.9 MB */

#define FLASH_SECTOR_ALIGN(addr)   ((addr) & ~(SPI_FLASH_SECTOR_SIZE - 1))

bool     SPI_Flash_Init(void);
bool     SPI_Flash_ReadID(uint8_t *mf, uint8_t *type, uint8_t *cap);
uint32_t SPI_Flash_ReadJedecID(void);
void     SPI_Flash_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);
bool     SPI_Flash_PageProgram(uint32_t addr, const uint8_t *data, uint16_t len);
bool     SPI_Flash_SectorErase(uint32_t addr);
bool     SPI_Flash_Block64Erase(uint32_t addr);
bool     SPI_Flash_ChipErase(void);
void     SPI_Flash_WriteEnable(void);
void     SPI_Flash_WaitBusy(void);
bool     SPI_Flash_Write(uint32_t addr, const uint8_t *data, uint32_t len);
bool     SPI_Flash_EraseRange(uint32_t addr, uint32_t size);

/* 字库读取辅助 */
void     SPI_Flash_ReadFontGB2312(uint16_t code, uint8_t *buf);

/* 字模格式转换: HZK16 row-major → OLED column-major (就地转换32字节) */
void     HZK16_To_OLED(uint8_t *buf);

#endif
