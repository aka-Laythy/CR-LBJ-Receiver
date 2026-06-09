#include "Menu.h"
#include "OLED.h"
#include "Key.h"
#include "Tick.h"
#include "GPS.h"
#include "SX1276/sx1276.h"
#include "SPI_Flash/spi_flash.h"
#include <string.h>
#include <stdio.h>

#define MENU_ITEM_COUNT     9
#define MENU_VISIBLE_ITEMS  4
#define ITEM_HEIGHT         16
#define MENU_FONT           OLED_8X16
#define INDICATOR_X         0
#define TEXT_X              8
#define MENU_ROW_WIDTH      120
#define PAGE_ROW_WIDTH      128
#define MAX_CONTENT_ROWS    12
#define CONTENT_LEN         40
#define SCROLL_POOL         12
#define SPLASH_DURATION_MS  500

#define KEY_REPEAT_DELAY_MS 400
#define KEY_REPEAT_RATE_MS  120
#define KEY_LONG_PRESS_MS   600

#define SCROLL_SPEED        2
#define SCROLL_INTERVAL_MS  30
#define SCROLL_PAUSE_MS     1200

#define FW_VERSION          1

static char *menu_items[MENU_ITEM_COUNT] = {
    "System Status",
    "Freq Settings",
    "Signal Detection",
    "Data Recording",
    "SX1276 Debug",
    "GPS Debug",
    "Storage Manage",
    "Network Config",
    "Power Off / Reboot",
};

static int8_t  selection = 0;
static int8_t  scroll_offset = 0;
static uint8_t prev_k1 = 0, prev_k2 = 0, prev_k3 = 0;
static uint8_t needs_redraw = 1;
static uint32_t k1_press_ms = 0, k1_last_rep = 0;
static uint32_t k2_press_ms = 0, k2_last_rep = 0;
static uint32_t k3_press_ms = 0;
static uint8_t  k3_suppress_release = 0;
static int8_t   content_row_start = 0;
static uint32_t page_refresh_ms = 0;

typedef enum { MENU_SPLASH, MENU_LIST, MENU_PAGE } MenuState_t;
static MenuState_t menu_state = MENU_SPLASH;
static uint32_t splash_start = 0;

static int16_t scroll_x[SCROLL_POOL];
static int8_t  scroll_dir[SCROLL_POOL];
static uint32_t scroll_last[SCROLL_POOL];
static uint32_t scroll_pause[SCROLL_POOL];

/*=============================================================================
 * 通用行水平滚动
 *===========================================================================*/
static int16_t row_scroll(int idx, const char *text, int16_t max_w)
{
    uint16_t tw = 0;
    const char *p = text;
    while (*p) {
        tw += ((uint8_t)*p < 0x80) ? 8 : 16;
        p += ((uint8_t)*p < 0x80) ? 1 : 3;
    }
    if (tw <= (uint16_t)max_w) return 0;

    int16_t max_x = tw - max_w + 2;
    uint32_t now = tick_ms;
    if (scroll_x[idx] > max_x) scroll_x[idx] = max_x;
    if (now < scroll_pause[idx] || now - scroll_last[idx] < SCROLL_INTERVAL_MS)
        return scroll_x[idx];
    scroll_last[idx] = now;
    scroll_x[idx] += scroll_dir[idx] * SCROLL_SPEED;
    if (scroll_x[idx] >= max_x) {
        scroll_x[idx] = max_x; scroll_dir[idx] = -1;
        scroll_pause[idx] = now + SCROLL_PAUSE_MS;
    } else if (scroll_x[idx] <= 0) {
        scroll_x[idx] = 0; scroll_dir[idx] = 1;
        scroll_pause[idx] = now + SCROLL_PAUSE_MS;
    }
    return scroll_x[idx];
}

static void make_numbered(char *buf, uint8_t size, uint8_t idx)
{
    uint8_t n = idx + 1, p = 0;
    if (n >= 10) buf[p++] = '0' + n / 10;
    buf[p++] = '0' + n % 10;
    buf[p++] = '.';
    buf[p] = 0;
    strncat(buf, menu_items[idx], size - p - 1);
}

/*=============================================================================
 * 主菜单
 *===========================================================================*/
static void draw_menu(void)
{
    OLED_Clear();
    for (int8_t i = 0; i < MENU_VISIBLE_ITEMS; i++) {
        int8_t idx = scroll_offset + i;
        if (idx >= MENU_ITEM_COUNT) break;
        char buf[36];
        make_numbered(buf, sizeof(buf), idx);
        if (idx == selection) {
            int16_t sx = row_scroll(i, buf, MENU_ROW_WIDTH);
            int16_t tx = TEXT_X - sx + (sx > 0 ? 2 : 0);
            OLED_ShowString(tx, i * 16, buf, MENU_FONT);
            OLED_ClearArea(0, i * 16, 7, 16);
            OLED_ShowChar(0, i * 16, '>', MENU_FONT);
        } else {
            OLED_ShowString(TEXT_X, i * 16, buf, MENU_FONT);
        }
    }
    OLED_Update();
}

/*=============================================================================
 * 内页内容填充
 *===========================================================================*/
static void page_sys_status(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    GPS_Data_TypeDef gps;
    bool ok = GPS_GetLatestData(&gps);
    sprintf(rows[0], "FW%04u", FW_VERSION);
    uint8_t mo = ok ? gps.month : 0, dy = ok ? gps.day : 0;
    if (ok)
        sprintf(rows[1], "%02u/%02u %02u:%02u:%02u %c",
                mo, dy, gps.hour, gps.minute, gps.second, gps.valid ? '+' : '-');
    else
        sprintf(rows[1], "--/-- --:--:-- -");
    *cnt = 2;
}

static void page_sx1276_debug(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    int16_t rssi = SX1276_ReadRSSI();
    sprintf(rows[0], "RSSI:%ddBm", rssi);

    uint8_t ah = SX1276_ReadReg(0x15), al = SX1276_ReadReg(0x16);
    int16_t afc = (int16_t)((ah << 8) | al);
    int32_t afc_hz = ((int32_t)afc * 15625) / 64;
    sprintf(rows[1], "AFC:%+05dHz", (int)afc_hz);

    uint8_t op = SX1276_ReadReg(0x01), vr = SX1276_ReadReg(0x42), ln = SX1276_ReadReg(0x0C);
    sprintf(rows[2], "OP:%02X V:%02X LN:%02X", op, vr, ln);
    uint8_t f1 = SX1276_ReadReg(0x06), f2 = SX1276_ReadReg(0x07), f3 = SX1276_ReadReg(0x08);
    uint8_t bh = SX1276_ReadReg(0x02), bl = SX1276_ReadReg(0x03);
    sprintf(rows[3], "F:%02X%02X%02X B:%02X%02X", f1, f2, f3, bh, bl);
    uint8_t fd = SX1276_ReadReg(0x05), bw = SX1276_ReadReg(0x12), pd = SX1276_ReadReg(0x1F);
    sprintf(rows[4], "FD:%02X BW:%02X P:%02X", fd, bw, pd);
    *cnt = 5;
}

static void page_gps_debug(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    GPS_Data_TypeDef gps;
    bool ok = GPS_GetLatestData(&gps);

    int32_t raw_lat = ok ? gps.latitude : 0;
    int32_t raw_lon = ok ? gps.longitude : 0;
    char lat_dir = ok ? gps.lat_dir : ' ';
    char lon_dir = ok ? gps.lon_dir : ' ';

    uint32_t alon = raw_lon < 0 ? (uint32_t)(-raw_lon) : (uint32_t)raw_lon;
    uint8_t  lon_d = (uint8_t)(alon / 1000000);
    uint16_t lon_m = (uint16_t)((alon % 1000000) * 60 / 1000000);
    uint16_t lon_s = (uint16_t)(((alon % 1000000) * 60 % 1000000) * 60 / 1000000);

    uint32_t alat = raw_lat < 0 ? (uint32_t)(-raw_lat) : (uint32_t)raw_lat;
    uint8_t  lat_d = (uint8_t)(alat / 1000000);
    uint16_t lat_m = (uint16_t)((alat % 1000000) * 60 / 1000000);
    uint16_t lat_s = (uint16_t)(((alat % 1000000) * 60 % 1000000) * 60 / 1000000);

    sprintf(rows[0], "Lon:%d*%02d'%02d\"%c", lon_d, (uint8_t)lon_m, (uint8_t)lon_s, lon_dir);
    sprintf(rows[1], "Lat:%d*%02d'%02d\"%c", lat_d, (uint8_t)lat_m, (uint8_t)lat_s, lat_dir);

    uint16_t yr = ok ? gps.year : 0;
    uint8_t mo = ok ? gps.month : 0, dy = ok ? gps.day : 0;
    sprintf(rows[2], "Date:%04u/%02u/%02u", yr, mo, dy);

    if (ok)
        sprintf(rows[3], "Time:%02u:%02u:%02u %c",
                gps.hour, gps.minute, gps.second, gps.valid ? '+' : '-');
    else
        sprintf(rows[3], "Time:--:--:-- ---");

    *cnt = 4;
}

static uint8_t flash_cap_mb(uint8_t cap)
{
    switch (cap) {
        case 0x14: return 1;
        case 0x15: return 2;
        case 0x16: return 4;
        case 0x17: return 8;
        case 0x18: return 16;
        case 0x19: return 32;
        case 0x20: return 64;
        default:   return 0;
    }
}

static void page_storage_mgmt(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    uint8_t mf, type, cap;
    bool ok = SPI_Flash_ReadID(&mf, &type, &cap);
    if (ok)
        sprintf(rows[0], "TC:%dMB ID:%02X%02X%02X",
                flash_cap_mb(cap), mf, type, cap);
    else
        sprintf(rows[0], "Flash not found");
    *cnt = 1;
}

static void (*const page_callbacks[MENU_ITEM_COUNT])(char[][CONTENT_LEN], uint8_t *) = {
    page_sys_status,     // 0
    0,                   // 1
    0,                   // 2
    0,                   // 3
    page_sx1276_debug,   // 4
    page_gps_debug,      // 5
    page_storage_mgmt,   // 6
    0,                   // 7
    0,                   // 8
};

/*=============================================================================
 * 内页绘制
 *===========================================================================*/
static void draw_page(void)
{
    char buf[36];
    make_numbered(buf, sizeof(buf), selection);
    uint8_t total = 0;
    char rows[MAX_CONTENT_ROWS][CONTENT_LEN];
    if (page_callbacks[selection]) page_callbacks[selection](rows, &total);

    OLED_Clear();
    int16_t sx = row_scroll(4, buf, PAGE_ROW_WIDTH);
    OLED_ShowString(-sx, 0, buf, MENU_FONT);

    int16_t start = content_row_start < 0 ? 0 : content_row_start;
    for (int8_t i = 0; i < 3; i++) {
        int16_t ri = start + i;
        if (ri >= (int16_t)total) break;
        int16_t sx = row_scroll(5 + i, rows[ri], PAGE_ROW_WIDTH);
        OLED_ShowString(-sx, 16 + i * 16, rows[ri], OLED_8X16);
    }
    OLED_Update();
}

/*=============================================================================
 * 按键
 *===========================================================================*/
static void select_up(void) {
    if (selection > 0) { selection--; if (selection < scroll_offset) scroll_offset = selection; needs_redraw = 1; }
}
static void select_down(void) {
    if (selection < MENU_ITEM_COUNT - 1) { selection++; if (selection >= scroll_offset + MENU_VISIBLE_ITEMS) scroll_offset = selection - MENU_VISIBLE_ITEMS + 1; needs_redraw = 1; }
}

static void handle_keys(void)
{
    uint8_t k1 = Key_IsPressed(KEY_K1), k2 = Key_IsPressed(KEY_K2), k3 = Key_IsPressed(KEY_K3);
    uint32_t now = tick_ms;

    if (menu_state == MENU_LIST) {
        if (k1) {
            if (!prev_k1) { k1_press_ms = now; select_up(); }
            else if (now - k1_press_ms >= KEY_REPEAT_DELAY_MS && now - k1_last_rep >= KEY_REPEAT_RATE_MS) { k1_last_rep = now; select_up(); }
        }
        if (k2) {
            if (!prev_k2) { k2_press_ms = now; select_down(); }
            else if (now - k2_press_ms >= KEY_REPEAT_DELAY_MS && now - k2_last_rep >= KEY_REPEAT_RATE_MS) { k2_last_rep = now; select_down(); }
        }
    } else if (menu_state == MENU_PAGE) {
        int8_t nv = 3;
        if (k1 && !prev_k1 && content_row_start > 0) { content_row_start--; needs_redraw = 1; }
        if (k2 && !prev_k2) {
            uint8_t total = 0; char r[MAX_CONTENT_ROWS][CONTENT_LEN];
            if (page_callbacks[selection]) page_callbacks[selection](r, &total);
            if (content_row_start <= (int16_t)total - nv) { content_row_start++; needs_redraw = 1; }
        }
    }
    prev_k1 = k1; prev_k2 = k2;

    if (k3 && !prev_k3) { k3_press_ms = now; k3_suppress_release = 0; }
    if (k3 && prev_k3 && menu_state == MENU_PAGE && now - k3_press_ms >= KEY_LONG_PRESS_MS) {
        menu_state = MENU_LIST; needs_redraw = 1; k3_suppress_release = 1;
    }
    if (!k3 && prev_k3 && !k3_suppress_release && menu_state == MENU_LIST) {
        menu_state = MENU_PAGE; needs_redraw = 1; content_row_start = 0;
    }
    prev_k3 = k3;
}

/*=============================================================================
 * 初始化
 *===========================================================================*/
void Menu_Init(void)
{
    OLED_Init();
    selection = 0; scroll_offset = 0;
    prev_k1 = prev_k2 = prev_k3 = 0;
    k3_suppress_release = 0; content_row_start = 0; page_refresh_ms = 0;
    memset(scroll_x, 0, sizeof(scroll_x));
    memset(scroll_dir, 0, sizeof(scroll_dir));
    menu_state = MENU_SPLASH;
    splash_start = tick_ms;
    needs_redraw = 1;
}

/*=============================================================================
 * 菜单任务
 *===========================================================================*/
void Menu_Task(void)
{
    switch (menu_state) {
    case MENU_SPLASH:
        if (needs_redraw) {
            OLED_Clear();
            OLED_ShowString(44, 24, "HELLO", OLED_8X16);
            OLED_Update(); needs_redraw = 0;
        }
        if (tick_ms - splash_start >= SPLASH_DURATION_MS) { menu_state = MENU_LIST; needs_redraw = 1; }
        break;

    case MENU_LIST:
        handle_keys();
        if (menu_state == MENU_PAGE) break;
        if (needs_redraw) { draw_menu(); needs_redraw = 0; } else {
            char buf[36]; make_numbered(buf, sizeof(buf), selection);
            if (row_scroll(selection - scroll_offset, buf, MENU_ROW_WIDTH)) draw_menu();
        }
        break;

    case MENU_PAGE:
        handle_keys();
        if (needs_redraw && menu_state == MENU_PAGE) { draw_page(); needs_redraw = 0; page_refresh_ms = tick_ms; } else if (menu_state == MENU_PAGE) {
            if (tick_ms - page_refresh_ms >= 1000) { page_refresh_ms = tick_ms; draw_page(); break; }
            uint8_t need = 0;
            char buf[36]; make_numbered(buf, sizeof(buf), selection);
            if (row_scroll(4, buf, PAGE_ROW_WIDTH)) need = 1;
            if (!need) {
                uint8_t total = 0; char rows[MAX_CONTENT_ROWS][CONTENT_LEN];
                if (page_callbacks[selection]) page_callbacks[selection](rows, &total);
                for (int i = 0; i < 6 && !need; i++) {
                    int ri = content_row_start + i;
                    if (ri >= (int)total) break;
                    if (row_scroll(5 + i, rows[ri], PAGE_ROW_WIDTH)) need = 1;
                }
            }
            if (need) draw_page();
        }
        break;
    }
}
