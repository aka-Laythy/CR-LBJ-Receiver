#include "spi_flash.h"
#include "SPI_Bus/spi_bus.h"
#include "Tick.h"
#include <string.h>

/*=============================================================================
 * 写使能
 *===========================================================================*/
void SPI_Flash_WriteEnable(void)
{
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_WRITE_ENABLE);
    SPI_Bus_Release();
}

/*=============================================================================
 * 等待 BUSY 位清除
 *===========================================================================*/
void SPI_Flash_WaitBusy(void)
{
    uint8_t sr;
    do {
        SPI_Bus_Select(SPI_CS_W25Q64);
        SPI_Bus_TransferByte(CMD_READ_STATUS1);
        sr = SPI_Bus_TransferByte(0xFF);
        SPI_Bus_Release();
    } while (sr & 0x01);
}

/*=============================================================================
 * 读 JEDEC ID（3 字节）
 *===========================================================================*/
uint32_t SPI_Flash_ReadJedecID(void)
{
    uint32_t id;
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_READ_JEDEC_ID);
    id = ((uint32_t)SPI_Bus_TransferByte(0xFF) << 16)
       | ((uint32_t)SPI_Bus_TransferByte(0xFF) << 8)
       | (uint32_t)SPI_Bus_TransferByte(0xFF);
    SPI_Bus_Release();
    return id;
}

bool SPI_Flash_ReadID(uint8_t *mf, uint8_t *type, uint8_t *cap)
{
    uint32_t id = SPI_Flash_ReadJedecID();
    if (mf)   *mf   = (uint8_t)(id >> 16);
    if (type) *type = (uint8_t)(id >> 8);
    if (cap)  *cap  = (uint8_t)(id);
    return (id >> 16) == SPI_FLASH_JEDEC_MF;
}

/*=============================================================================
 * 读数据
 *===========================================================================*/
void SPI_Flash_ReadData(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_READ_DATA);
    SPI_Bus_TransferByte((uint8_t)(addr >> 16));
    SPI_Bus_TransferByte((uint8_t)(addr >> 8));
    SPI_Bus_TransferByte((uint8_t)(addr));
    SPI_Bus_Read(buf, len);
    SPI_Bus_Release();
}

/*=============================================================================
 * 页编程（一页最多 256 字节）
 *===========================================================================*/
bool SPI_Flash_PageProgram(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (len > SPI_FLASH_PAGE_SIZE) return false;

    SPI_Flash_WriteEnable();
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_PAGE_PROGRAM);
    SPI_Bus_TransferByte((uint8_t)(addr >> 16));
    SPI_Bus_TransferByte((uint8_t)(addr >> 8));
    SPI_Bus_TransferByte((uint8_t)(addr));
    SPI_Bus_Write((uint8_t *)data, len);
    SPI_Bus_Release();
    SPI_Flash_WaitBusy();
    return true;
}

/*=============================================================================
 * 连续写（跨页自动分割）
 *===========================================================================*/
bool SPI_Flash_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len > 0) {
        uint32_t page_end = (addr / SPI_FLASH_PAGE_SIZE + 1) * SPI_FLASH_PAGE_SIZE;
        uint16_t chunk = (uint16_t)(page_end - addr);
        if (chunk > len) chunk = (uint16_t)len;
        if (!SPI_Flash_PageProgram(addr, data, chunk))
            return false;
        addr += chunk;
        data += chunk;
        len  -= chunk;
    }
    return true;
}

/*=============================================================================
 * 扇区擦除（4KB）
 *===========================================================================*/
bool SPI_Flash_SectorErase(uint32_t addr)
{
    SPI_Flash_WriteEnable();
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_SECTOR_ERASE);
    SPI_Bus_TransferByte((uint8_t)(addr >> 16));
    SPI_Bus_TransferByte((uint8_t)(addr >> 8));
    SPI_Bus_TransferByte((uint8_t)(addr));
    SPI_Bus_Release();
    SPI_Flash_WaitBusy();
    return true;
}

/*=============================================================================
 * 64KB 块擦除
 *===========================================================================*/
bool SPI_Flash_Block64Erase(uint32_t addr)
{
    SPI_Flash_WriteEnable();
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_BLOCK64_ERASE);
    SPI_Bus_TransferByte((uint8_t)(addr >> 16));
    SPI_Bus_TransferByte((uint8_t)(addr >> 8));
    SPI_Bus_TransferByte((uint8_t)(addr));
    SPI_Bus_Release();
    SPI_Flash_WaitBusy();
    return true;
}

/*=============================================================================
 * 整片擦除
 *===========================================================================*/
bool SPI_Flash_ChipErase(void)
{
    SPI_Flash_WriteEnable();
    SPI_Bus_Select(SPI_CS_W25Q64);
    SPI_Bus_TransferByte(CMD_CHIP_ERASE);
    SPI_Bus_Release();
    SPI_Flash_WaitBusy();
    return true;
}

/*=============================================================================
 * 初始化：读 ID 确认芯片
 *===========================================================================*/
bool SPI_Flash_Init(void)
{
    uint8_t mf, type, cap;
    if (!SPI_Flash_ReadID(&mf, &type, &cap))
        return false;
    return (mf == SPI_FLASH_JEDEC_MF && type == SPI_FLASH_JEDEC_TYPE);
}
