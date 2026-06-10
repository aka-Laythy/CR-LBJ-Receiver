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
 * 范围擦除（按 4KB 扇区）
 *===========================================================================*/
bool SPI_Flash_EraseRange(uint32_t addr, uint32_t size)
{
    uint32_t end = addr + size;
    addr = FLASH_SECTOR_ALIGN(addr);
    while (addr < end) {
        if (addr % SPI_FLASH_BLOCK64_SIZE == 0 && end - addr >= SPI_FLASH_BLOCK64_SIZE) {
            if (!SPI_Flash_Block64Erase(addr)) return false;
            addr += SPI_FLASH_BLOCK64_SIZE;
        } else {
            if (!SPI_Flash_SectorErase(addr)) return false;
            addr += SPI_FLASH_SECTOR_SIZE;
        }
    }
    return true;
}

/*=============================================================================
 * 读取 GB2312 汉字字模（32 字节 16×16 点阵）
 * code: GB2312 内码（高字节=区码+0xA0，低字节=位码+0xA0）
 * buf: 输出 32 字节点阵数据
 *===========================================================================*/
void SPI_Flash_ReadFontGB2312(uint16_t code, uint8_t *buf)
{
    /* ASCII 字符从字库开头顺序存，每字 16 字节 */
    if (code < 0x80) {
        uint32_t addr = FLASH_PART_FONT_ADDR + (uint32_t)code * 16;
        SPI_Flash_ReadData(addr, buf, 16);
        /* 下半部分填 0（16×16 中 ASCII 只占 8×16，低 8 行空白） */
        memset(buf + 16, 0, 16);
        return;
    }

    /* GB2312 汉字 */
    uint8_t qu = (uint8_t)(code >> 8) - 0xA1;
    uint8_t wei = (uint8_t)(code & 0xFF) - 0xA1;
    if (qu > 86 || wei > 93) {
        memset(buf, 0, 32);
        return;
    }
    uint32_t idx = (uint32_t)qu * 94 + wei;
    uint32_t addr = FLASH_PART_FONT_ADDR + idx * 32;
    SPI_Flash_ReadData(addr, buf, 32);
}

/*=============================================================================
 * HZK16 row-major -> OLED column-major 转置
 * 就地转换32字节点阵数据
 *
 * HZK16: byte[r*2].bit7=col0 ... bit0=col7, byte[r*2+1].bit7=col8 ... bit0=col15
 * OLED:  byte[col + 0].bit0=row0 ... bit7=row7, byte[col + 16].bit0=row8 ... bit7=row15
 *===========================================================================*/
void HZK16_To_OLED(uint8_t *buf)
{
    uint8_t tmp[32];
    uint8_t col, row, page, bit_in_hzk;
    uint8_t hzk_idx;

    memset(tmp, 0, 32);
    for (col = 0; col < 16; col++) {
        for (row = 0; row < 16; row++) {
            hzk_idx = row * 2 + (col < 8 ? 0 : 1);
            bit_in_hzk = 7 - (col & 0x07);
            if (buf[hzk_idx] & (1 << bit_in_hzk)) {
                page = row >> 3;
                tmp[col + page * 16] |= (uint8_t)(1 << (row & 0x07));
            }
        }
    }
    memcpy(buf, tmp, 32);
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
