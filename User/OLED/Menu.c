#include "Menu.h"
#include "OLED.h"
#include "Key.h"
#include "Tick.h"
#include "GPS.h"
#include "SX1276/sx1276.h"
#include "SPI_Flash/spi_flash.h"
#include "LBJ/lbj.h"
#include "BLE.h"
#include <string.h>
#include "str_util.h"

#define MENU_ITEM_COUNT     8
#define MENU_VISIBLE_ITEMS  4
#define ITEM_HEIGHT         16
#define MENU_FONT           OLED_8X16
#define INDICATOR_X         0
#define TEXT_X              8
#define MENU_ROW_WIDTH      120
#define PAGE_ROW_WIDTH      128
#define MAX_CONTENT_ROWS    6
#define CONTENT_LEN         40
#define SCROLL_POOL         12
#define SPLASH_DURATION_MS  500

#define KEY_REPEAT_DELAY_MS 400
#define KEY_REPEAT_RATE_MS  120
#define KEY_LONG_PRESS_MS   600

#define SCROLL_SPEED        2
#define SCROLL_INTERVAL_MS  30
#define SCROLL_PAUSE_MS     1200

#define FW_VERSION          2           // 定义固件版本

/* GB2312 编码的菜单字符串 */
static const uint8_t gb_menu_00[] = {0xCF,0xB5,0xCD,0xB3,0xD7,0xB4,0xCC,0xAC,0x00}; // 系统状态
static const uint8_t gb_menu_01[] = {0xD0,0xC5,0xBA,0xC5,0xBC,0xEC,0xB2,0xE2,0x00}; // 信号检测
static const uint8_t gb_menu_02[] = {0xCA,0xFD,0xBE,0xDD,0xBC,0xC7,0xC2,0xBC,0x00}; // 数据记录
static const uint8_t gb_menu_03[] = {0xC9,0xE4,0xC6,0xB5,0xB5,0xF7,0xCA,0xD4,0x00}; // 射频调试
static const uint8_t gb_menu_04[] = {0xB6,0xA8,0xCE,0xBB,0xB5,0xF7,0xCA,0xD4,0x00}; // 定位调试
static const uint8_t gb_menu_05[] = {0xB4,0xE6,0xB4,0xA2,0xB9,0xDC,0xC0,0xED,0x00}; // 存储管理
static const uint8_t gb_menu_06[] = {0xCD,0xF8,0xC2,0xE7,0xC5,0xE4,0xD6,0xC3,0x00}; // 网络配置
static const uint8_t gb_menu_07[] = {0xC6,0xE4,0xCB,0xFB,0xB2,0xE2,0xCA,0xD4,0x00}; // 其他测试

static const char *menu_items[MENU_ITEM_COUNT] = {
    (const char *)gb_menu_00,
    (const char *)gb_menu_01,
    (const char *)gb_menu_02,
    (const char *)gb_menu_03,
    (const char *)gb_menu_04,
    (const char *)gb_menu_05,
    (const char *)gb_menu_06,
    (const char *)gb_menu_07,
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
        p += ((uint8_t)*p < 0x80) ? 1 : 2;
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
            OLED_ShowGBString(tx, i * 16, (const uint8_t *)buf, MENU_FONT);
            OLED_ClearArea(0, i * 16, 7, 16);
            OLED_ShowChar(0, i * 16, '>', MENU_FONT);
        } else {
            OLED_ShowGBString(TEXT_X, i * 16, (const uint8_t *)buf, MENU_FONT);
        }
    }
    OLED_Update();
}

/*=============================================================================
 * 内页内容填充
 *===========================================================================*/
static void page_sys_status(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    uint8_t p;
    p = 0;
    memcpy(rows[0], "\xB9\xCC\xBC\xFE", 4); p += 4;  // 固件
    p += (uint8_t)mini_sprintf(rows[0] + p, ":V%03u", FW_VERSION);
    rows[0][p] = '\0';

    GPS_Data_TypeDef gps;
    bool ok = GPS_GetLatestData(&gps);
    uint8_t mo = ok ? gps.month : 0, dy = ok ? gps.day : 0;
    if (ok)
        mini_sprintf(rows[1], "%02u/%02u %02u:%02u:%02u %c",
                mo, dy, gps.hour, gps.minute, gps.second, gps.valid ? '+' : '-');
    else
        mini_sprintf(rows[1], "--/-- --:--:-- -");
    *cnt = 2;
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

    uint16_t yr = ok ? gps.year : 0;
    uint8_t mo = ok ? gps.month : 0, dy = ok ? gps.day : 0;
    uint8_t p;

    p = 0;
    memcpy(rows[0], "\xBE\xAD\xB6\xC8", 4); p += 4;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[0] + p, ":%d*%02d'%02d\"%c", lon_d, (uint8_t)lon_m, (uint8_t)lon_s, lon_dir);
    else
        { memcpy(rows[0] + p, ":----", 5); p += 5; }
    rows[0][p] = '\0';

    p = 0;
    memcpy(rows[1], "\xCE\xB3\xB6\xC8", 4); p += 4;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[1] + p, ":%d*%02d'%02d\"%c", lat_d, (uint8_t)lat_m, (uint8_t)lat_s, lat_dir);
    else
        { memcpy(rows[1] + p, ":----", 5); p += 5; }
    rows[1][p] = '\0';

    p = 0;
    memcpy(rows[2], "\xC8\xD5\xC6\xDA", 4); p += 4;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[2] + p, ":%04u/%02u/%02u", yr, mo, dy);
    else
        { memcpy(rows[2] + p, ":----/--/--", 11); p += 11; }
    rows[2][p] = '\0';

    p = 0;
    memcpy(rows[3], "\xCA\xB1\xBC\xE4", 4); p += 4;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[3] + p, ":%02u:%02u:%02u %c",
                gps.hour, gps.minute, gps.second, gps.valid ? '+' : '-');
    else
        { memcpy(rows[3] + p, ":--:--:-- ---", 13); p += 13; }
    rows[3][p] = '\0';
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

/* ================================================================
 *  SX1276 BW 查表: 寄存器高 4 位 → 双边带带宽 (kHz×10)
 *  SX1276 的 BW 由 RegValue[7:4] (高 4 位) 决定,
 *  低 4 位是 FSK 频谱杂散优化(抖动), 不影响带宽。
 *  参照 SX1276 数据手册 Rev7 表14 @ 32MHz.
 * ================================================================ */
static const uint16_t sx1276_bw_tab[16] = {
    104, 208, 312, 417, 625, 834, 1250, 2500, 5000, 10000
};
static uint16_t bw_lookup(uint8_t reg) {
    uint8_t idx = reg >> 4;
    if (idx > 9) idx = 9;
    return sx1276_bw_tab[idx];
}
/* ================================================================
 *  SX1276 射频调试页面 (selection==3)
 *  6 槽 × 16px, 一次显示 3 槽.
 *  槽 0-3: 大字 (RSSI / AFC / AFCBW / RXBW) 各一行
 *  槽 4-5: 各含 2 行紧贴小字 (寄存器明细)
 *  每按一次滚动 16px (1 槽).
 * ================================================================ */
#define SX1276_SLOTS   6
#define SX1276_NV      3

static void draw_sx1276_page(void)
{
    int16_t rssi = SX1276_ReadRSSI();
    uint8_t ah = SX1276_ReadReg(0x15), al = SX1276_ReadReg(0x16);
    int16_t afc = (int16_t)((ah << 8) | al);
    /* AFC 步进 = Fxosc / 2^19 = 32e6/524288 ≈ 61.035 Hz = 15625/256 */
    int32_t afc_hz = ((int32_t)afc * 15625) / 256;
    uint8_t op = SX1276_ReadReg(0x01), vr = SX1276_ReadReg(0x42), ln = SX1276_ReadReg(0x0C);
    uint8_t f1 = SX1276_ReadReg(0x06), f2 = SX1276_ReadReg(0x07), f3 = SX1276_ReadReg(0x08);
    uint8_t bh = SX1276_ReadReg(0x02), bl = SX1276_ReadReg(0x03);
    uint8_t fd = SX1276_ReadReg(0x05), bw = SX1276_ReadReg(0x12), pd = SX1276_ReadReg(0x1F);
    uint8_t abw = SX1276_ReadReg(0x13), rc = SX1276_ReadReg(0x0D);

    char line[24];

    for (int8_t i = 0; i < SX1276_NV; i++) {
        int8_t slot = content_row_start + i;
        if (slot >= SX1276_SLOTS) break;
        uint8_t y = (uint8_t)(16 + i * 16);
        uint16_t bwv;

        switch (slot) {
        case 0:
            mini_sprintf(line, "RSSI:%ddBm", rssi);
            OLED_ShowString(8, y, line, OLED_8X16);
            break;
        case 1:
            mini_sprintf(line, "AFC:%+05dHz", (int)afc_hz);
            OLED_ShowString(8, y, line, OLED_8X16);
            break;
        case 2:
            bwv = bw_lookup(abw);
            mini_sprintf(line, "AFCBW:%u.%ukHz", (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
            OLED_ShowString(8, y, line, OLED_8X16);
            break;
        case 3:
            bwv = bw_lookup(bw);
            mini_sprintf(line, "RXBW:%u.%ukHz", (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
            OLED_ShowString(8, y, line, OLED_8X16);
            break;
        case 4:
            mini_sprintf(line, "OP:%02X V:%02X LN:%02X", op, vr, ln);
            OLED_ShowString(8, y,     line, OLED_6X8);
            mini_sprintf(line, "FR:%02X%02X%02X BR:%02X%02X", f1, f2, f3, bh, bl);
            OLED_ShowString(8, y + 8, line, OLED_6X8);
            break;
        case 5:
            mini_sprintf(line, "FD:%02X BW:%02X PD:%02X", fd, bw, pd);
            OLED_ShowString(8, y,     line, OLED_6X8);
            mini_sprintf(line, "RC:%02X DI:%02X AB:%02X", rc, 0xC0, abw);
            OLED_ShowString(8, y + 8, line, OLED_6X8);
            break;
        }
    }
}

static void page_storage_mgmt(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    uint8_t mf, type, cap;
    bool ok = SPI_Flash_ReadID(&mf, &type, &cap);
    uint8_t p = 0;
    memcpy(rows[0], "\xD7\xDC", 2); p += 2;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[0] + p, ":%dMB ID:%02X%02X%02X",
                flash_cap_mb(cap), mf, type, cap);
    else {
        memcpy(rows[0] + p, ":无Flash", 10); p += 10;
    }
    rows[0][p] = '\0';
    *cnt = 1;
}

/* GB2312 标签: 车次 速度 公里 机车 线路 */
static const uint8_t gb_l_cs[] = {0xB3,0xB5,0xB4,0xCE,0x00};  // 车次
static const uint8_t gb_l_sd[] = {0xCB,0xD9,0xB6,0xC8,0x00};  // 速度
static const uint8_t gb_l_gl[] = {0xB9,0xAB,0xC0,0xEF,0x00};  // 公里
static const uint8_t gb_l_jc[] = {0xBB,0xFA,0xB3,0xB5,0x00};  // 机车
static const uint8_t gb_l_xl[] = {0xCF,0xDF,0xC2,0xB7,0x00};  // 线路

static void draw_signal_detect_page(void)
{
    const LBJ_Message_t *msg = LBJ_GetLatest();
    if (!msg || !msg->has_train_id) {
        OLED_ShowGBString(8, 24, (const uint8_t *)"\xB5\xC8\xB4\xFD\xD0\xC5\xBA\xC5\x2E\x2E\x2E", OLED_8X16);
        return;
    }

    uint8_t buf[64];
    uint8_t p;
    int16_t y = 16;

    /* Row 0: 车次: XXXXX */
    p = 0;
    memcpy(buf + p, gb_l_cs, 4); p += 4;
    buf[p++] = ':'; buf[p++] = ' ';
    {
        uint8_t tl = (uint8_t)strlen(msg->train_id);
        if (tl > 30) tl = 30;
        memcpy(buf + p, msg->train_id, tl); p += tl;
    }
    buf[p] = '\0';
    OLED_ShowGBString(8, y, buf, OLED_8X16);
    y += 16;

    /* Row 1: 速度: XXX km/h */
    if (msg->has_speed) {
        p = 0;
        memcpy(buf + p, gb_l_sd, 4); p += 4;
        p += (uint8_t)mini_sprintf((char *)(buf + p), ": %d km/h", msg->speed);
        buf[p] = '\0';
    } else {
        p = 0;
        memcpy(buf + p, gb_l_sd, 4); p += 4;
        memcpy(buf + p, ": ---", 5); p += 5;
        buf[p] = '\0';
    }
    OLED_ShowGBString(8, y, buf, OLED_8X16);
    y += 16;

    /* Row 2: 公里: XX.X km */
    p = 0;
    memcpy(buf + p, gb_l_gl, 4); p += 4;
    buf[p++] = ':'; buf[p++] = ' ';
    if (msg->has_km) {
        uint8_t klen = (uint8_t)strlen(msg->km_post);
        if (klen >= 5) {
            memcpy(buf + p, msg->km_post, 4); p += 4;
            buf[p++] = '.';
            buf[p++] = msg->km_post[4];
        } else if (klen >= 2) {
            memcpy(buf + p, msg->km_post, klen - 1); p += klen - 1;
            buf[p++] = '.';
            buf[p++] = msg->km_post[klen - 1];
        } else if (klen == 1) {
            buf[p++] = '0';
            buf[p++] = '.';
            buf[p++] = msg->km_post[0];
        } else {
            memcpy(buf + p, "---.-", 5); p += 5;
        }
        memcpy(buf + p, " km", 3); p += 3;
    } else {
        memcpy(buf + p, "---.- km", 8); p += 8;
    }
    buf[p] = '\0';
    OLED_ShowGBString(8, y, buf, OLED_8X16);
    y += 16;

    /* Row 3: 机车: 型号 编号 */
    p = 0;
    memcpy(buf + p, gb_l_jc, 4); p += 4;
    buf[p++] = ':'; buf[p++] = ' ';
    if (msg->is_extended && msg->loco_model[0]) {
        uint8_t ml = (uint8_t)strlen(msg->loco_model);
        memcpy(buf + p, msg->loco_model, ml); p += ml;
        if (msg->loco_number[0]) {
            buf[p++] = '-';
            uint8_t nl = (uint8_t)strlen(msg->loco_number);
            memcpy(buf + p, msg->loco_number, nl); p += nl;
        }
    } else {
        memcpy(buf + p, "----", 4); p += 4;
    }
    buf[p] = '\0';
    OLED_ShowGBString(8, y, buf, OLED_8X16);
    y += 16;

    /* Row 4: 线路: 中文线路名 (GBK 字节) */
    p = 0;
    memcpy(buf + p, gb_l_xl, 4); p += 4;
    buf[p++] = ':'; buf[p++] = ' ';
    if (msg->is_extended && msg->route_gbk_len > 0 && msg->route_gbk_len < 30) {
        memcpy(buf + p, msg->route_gbk, msg->route_gbk_len);
        p += msg->route_gbk_len;
    } else {
        memcpy(buf + p, "----", 4); p += 4;
    }
    buf[p] = '\0';
    OLED_ShowGBString(8, y, buf, OLED_8X16);
}

static void page_other_debug(char rows[][CONTENT_LEN], uint8_t *cnt)
{
    *cnt = 0;
}

static void (*const page_callbacks[MENU_ITEM_COUNT])(char[][CONTENT_LEN], uint8_t *) = {
    page_sys_status,     // 0: 系统状态
    0,                   // 1: 信号检测 (draw_page 内自定义绘制)
    0,                   // 2: 数据记录
    0,                   // 3: 射频调试 (draw_sx1276_page 内自定义绘制)
    page_gps_debug,      // 4: 定位调试
    page_storage_mgmt,   // 5: 存储管理
    0,                   // 6: 网络配置
    page_other_debug,    // 7: 其他测试
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
    OLED_ShowGBString(-sx, 0, (const uint8_t *)buf, MENU_FONT);

    if (selection == 1) {
        draw_signal_detect_page();
    } else if (selection == 3) {
        draw_sx1276_page();
    } else if (selection == 7) {
        extern uint8_t OLED_DisplayBuf[8][128];
        static const uint16_t gb[] = {
            0xBABA, 0xD7D6, 0xCFD4, 0xCABE,
            0xB2E2, 0xCAD4, 0xB3C9, 0xB9A6
        };
        uint8_t fb[32];
        for (uint8_t i = 0; i < 8; i++) {
            SPI_Flash_ReadFontGB2312(gb[i], fb);
            HZK16_To_OLED(fb);
            for (uint8_t c = 0; c < 16; c++) {
                OLED_DisplayBuf[2][i*16 + c] = fb[c];
                OLED_DisplayBuf[3][i*16 + c] = fb[c + 16];
            }
        }
    } else {
        int16_t start = content_row_start < 0 ? 0 : content_row_start;
        for (int8_t i = 0; i < 3; i++) {
            int16_t ri = start + i;
            if (ri >= (int16_t)total) break;
            int16_t sx = row_scroll(5 + i, rows[ri], PAGE_ROW_WIDTH);
            OLED_ShowGBString(-sx, 16 + i * 16, (const uint8_t *)rows[ri], OLED_8X16);
        }
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
    static uint8_t _dbg_once = 0;
    if (!_dbg_once) {
        _dbg_once = 1;
        char _dbgbuf[32];
        mini_sprintf(_dbgbuf, "[KEY] k1=%d k2=%d k3=%d ms=%lu\r\n", k1, k2, k3, (unsigned long)now);
        BLE_SendString((const uint8_t *)_dbgbuf);
    }

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
        if (selection == 3) {
            /* SX1276 页: 5 槽统一滚动, nv=3, max_start=2 */
            if (k1 && !prev_k1 && content_row_start > 0) { content_row_start--; needs_redraw = 1; }
            if (k2 && !prev_k2 && content_row_start < 3) { content_row_start++; needs_redraw = 1; }
        } else {
            int8_t nv = 3;
            if (k1 && !prev_k1 && content_row_start > 0) { content_row_start--; needs_redraw = 1; }
            if (k2 && !prev_k2) {
                uint8_t total = 0; char r[MAX_CONTENT_ROWS][CONTENT_LEN];
                if (page_callbacks[selection]) page_callbacks[selection](r, &total);
                if ((int16_t)content_row_start <= (int16_t)total - nv) { content_row_start++; needs_redraw = 1; }
            }
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
            OLED_ShowString(48, 16, "你好", OLED_8X16);
            OLED_ShowString(0, 32, "交通强国铁路先行", OLED_8X16);
            OLED_Update(); needs_redraw = 0;
        }
        {
            static uint16_t splash_cnt = 0;
            if (tick_ms - splash_start >= SPLASH_DURATION_MS || ++splash_cnt > 500) {
                menu_state = MENU_LIST; needs_redraw = 1;
                BLE_SendString((const uint8_t *)"[MENU] splash->list\r\n");
            }
        }
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
