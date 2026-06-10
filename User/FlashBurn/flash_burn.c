#include "flash_burn.h"
#include "SPI_Flash/spi_flash.h"
#include "BLE.h"
#include "OLED/OLED.h"
#include "Tick.h"
#include <string.h>
#include "str_util.h"
#include <stdint.h>

/* 烧录完成后死循环等待看门狗或手动复位 */
/* CH32V005 RISC-V 无统一软复位寄存器，暂时用 WFI 等待复位 */
static void wait_reset(void)
{
    __disable_irq();
    while (1) {
        __asm__ volatile("wfi");
    }
}

#define BURN_PAGE_SIZE  256
#define LINE_BUF_LEN    64

typedef enum {
    BURN_IDLE,
    BURN_WRITING,
} BurnState_t;

volatile bool flash_burn_active = false;

static BurnState_t  burn_state = BURN_IDLE;
static uint32_t     burn_addr = 0;
static uint32_t     burn_written = 0;
static uint8_t      line_buf[LINE_BUF_LEN];
static uint16_t     line_len = 0;
static uint32_t     data_pending = 0;
static uint32_t     data_addr = 0;
static uint8_t      page_buf[BURN_PAGE_SIZE];
static uint16_t     page_filled = 0;
static char         resp_buf[80];

static void send_resp(const char *s)
{
    BLE_SendData((const uint8_t *)s, (uint16_t)strlen(s));
}

static void process_line(void)
{
    if (line_len == 0) return;
    line_buf[line_len < LINE_BUF_LEN ? line_len : LINE_BUF_LEN - 1] = '\0';
    char *line = (char *)line_buf;

    if (strcmp(line, "fire font") == 0) {
        burn_state = BURN_WRITING;
        burn_addr = FLASH_PART_FONT_ADDR;
        burn_written = 0;
        flash_burn_active = true;
        /* 清屏 + 居中显示烧录提示 */
        OLED_Clear();
        OLED_ShowString(4, 24, "Burning Chinese", OLED_8X16);
        OLED_ShowString(20, 40, "Font...", OLED_8X16);
        OLED_Update();
        send_resp("[BURN] READY\n");
        return;
    }

    if (strcmp(line, "BURN_ERASE") == 0) {
        SPI_Flash_EraseRange(FLASH_PART_FONT_ADDR, FLASH_PART_FONT_SIZE);
        send_resp("[BURN] ERASE_DONE\n");
        return;
    }

    if (memcmp(line, "BURN_DATA ", 10) == 0) {
        char *addr_str = line + 10;
        data_addr = mini_atou32(addr_str);
        data_pending = BURN_PAGE_SIZE;
        page_filled = 0;
        send_resp("[BURN] RDY\n");
        return;
    }

    if (strcmp(line, "BURN_END") == 0) {
        burn_state = BURN_IDLE;
        flash_burn_active = false;
        /* OLED 显示完成 */
        OLED_Clear();
        OLED_ShowString(36, 24, "Done!", OLED_8X16);
        OLED_Update();
        Tick_DelayMs(300);
        send_resp("[BURN] DONE\n");
        Tick_DelayMs(100);
        wait_reset();
        return;
    }

    if (memcmp(line, "BURN_VERIFY", 11) == 0) {
        uint8_t v[64];
        SPI_Flash_ReadData(FLASH_PART_FONT_ADDR, v, 64);
        mini_sprintf(resp_buf, "[BURN] V: %02X%02X%02X%02X...%02X%02X\n",
                v[0], v[1], v[2], v[3], v[60], v[61]);
        send_resp(resp_buf);
        return;
    }

    if (memcmp(line, "BURN_READBACK ", 14) == 0) {
        uint32_t raddr = 0, rlen = 0;
        char *p = line + 14;
        while (*p >= '0' && *p <= '9') { raddr = raddr * 10 + (*p - '0'); p++; }
        if (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { rlen = rlen * 10 + (*p - '0'); p++; }
        if (rlen == 0 || rlen > 4096) rlen = 4096;
        uint8_t rbuf[256];
        send_resp("[BURN] READBACK\n");
        while (rlen > 0) {
            uint16_t chk = (rlen > 256) ? 256 : (uint16_t)rlen;
            SPI_Flash_ReadData(raddr, rbuf, chk);
            BLE_SendData(rbuf, chk);
            raddr += chk;
            rlen  -= chk;
            /* 等 TX 缓冲排空 */
            Tick_DelayMs(3);
        }
        return;
    }
}

void FlashBurn_Init(void)
{
    burn_state = BURN_IDLE;
    line_len = 0;
    data_pending = 0;
}

void FlashBurn_Task(void)
{
    uint8_t byte;

    while (BLE_ReadByte(&byte)) {
        /* 优先接收数据页 */
        if (data_pending > 0) {
            page_buf[page_filled++] = byte;
            data_pending--;
            if (data_pending == 0) {
                /* 一页收齐，写入 W25Q64 */
                SPI_Flash_PageProgram(data_addr, page_buf, BURN_PAGE_SIZE);
                burn_written += BURN_PAGE_SIZE;
                mini_sprintf(resp_buf, "[BURN] OK %lu\n", (unsigned long)burn_written);
                send_resp(resp_buf);
            }
            continue;
        }

        /* 接收文本行 */
        if (byte == '\n') {
            process_line();
            line_len = 0;
        } else if (line_len < LINE_BUF_LEN - 1) {
            line_buf[line_len++] = byte;
        }
    }
}