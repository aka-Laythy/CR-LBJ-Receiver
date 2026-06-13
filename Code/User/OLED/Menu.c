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
        uint8_t b = (uint8_t)*p;
        tw += (b < 0x80 || b == 0xB0) ? 8 : 16;
        p += (b < 0x80 || b == 0xB0) ? 1 : 2;
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
        p += (uint8_t)mini_sprintf(rows[0] + p, ":%d%c%02d'%02d\"%c", lon_d, 0xB0, (uint8_t)lon_m, (uint8_t)lon_s, lon_dir);
    else
        { memcpy(rows[0] + p, ":----", 5); p += 5; }
    rows[0][p] = '\0';

    p = 0;
    memcpy(rows[1], "\xCE\xB3\xB6\xC8", 4); p += 4;
    if (ok)
        p += (uint8_t)mini_sprintf(rows[1] + p, ":%d%c%02d'%02d\"%c", lat_d, 0xB0, (uint8_t)lat_m, (uint8_t)lat_s, lat_dir);
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
 *  SX1276 BW 查找: RegRxBw (0x12) / RegAfcBw (0x13)
 *
 *  RegRxBw (0x12) 位域:
 *    bits[6:5] = reserved, 必须写 00
 *    bits[4:3] = RxBwMant:  00→16, 01→20, 10→24
 *    bits[2:0] = RxBwExp:   1..7 (Exp=0 为保留值)
 *
 *  RegAfcBw (0x13) 位域:
 *    bits[7:5] = reserved, 必须写 000
 *    bits[4:3] = RxBwMantAfc: 编码同上
 *    bits[2:0] = RxBwExpAfc:  1..7
 *
 *  公式: RxBw_SSB(Hz) = FXOSC(32MHz) / (Mant × 2^(Exp+2))
 *  下表的"等效带宽" = SSB × 2, 为工程参考值
 *
 *  可选的寄存器值及对应带宽:
 *    idx  等效 BW(kHz)  regval   Mant Exp
 *     0      5.2    0x17    24    7
 *     1      6.2    0x0F    20    7
 *     2      7.8    0x07    16    7
 *     3     10.4    0x16    24    6
 *     4     12.6    0x0E    20    6
 *     5     15.6    0x06    16    6
 *     6     20.8    0x15    24    5   (RXBW 当前值, 手册 SSB=10.4kHz)
 *     7     25.0    0x0D    20    5
 *     8     31.3    0x05    16    5
 *     9     41.6    0x14    24    4
 *    10     50.0    0x0C    20    4
 *    11     62.5    0x04    16    4
 *    12     83.3    0x13    24    3
 *    13    100.0    0x0B    20    3   (AFCBW 复位值, 手册 SSB=50kHz)
 *    14    125.0    0x03    16    3
 *    15    166.7    0x12    24    2
 *    16    200.0    0x0A    20    2
 *    17    250.0    0x02    16    2
 *    18    333.3    0x11    24    1
 *    19    400.0    0x09    20    1
 *    20    500.0    0x01    16    1   (AFCBW 当前值, 手册 SSB=250kHz)
 * ================================================================ */
#define BW_ENTRIES 21
/* 等效双边带宽 = 2×SSB, 存储值 = kHz×10, 显示时除以10 */
static const uint16_t bw_equiv_khz10[BW_ENTRIES] = {
    52,   62,   78,   104,  125,  156,
    208,  250,  313,  416,  500,  625,
    833,  1000, 1250, 1667, 2000, 2500,
    3333, 4000, 5000
};
static const uint8_t bw_regval[BW_ENTRIES] = {
    0x17, 0x0F, 0x07, 0x16, 0x0E, 0x06,
    0x15, 0x0D, 0x05, 0x14, 0x0C, 0x04,
    0x13, 0x0B, 0x03, 0x12, 0x0A, 0x02,
    0x11, 0x09, 0x01
};

/* 查找寄存器值对应的 BW 索引, 找不到返回默认索引 */
static int8_t bw_idx_from_reg(uint8_t regval)
{
    uint8_t masked = regval & 0x1F;  /* bits[4:0] */
    for (int8_t i = 0; i < BW_ENTRIES; i++) {
        if (bw_regval[i] == masked) return i;
    }
    return 6; /* 默认 20.8kHz (RxBw 复位值) */
}
/* ================================================================
 *  射频调试内页: 设置子状态机
 * ================================================================ */
typedef enum {
    BW_IDLE,       /* 正常浏览 */
    BW_SET_AFCBW,  /* 正在设置 AFCBW */
    BW_SET_RXBW,   /* 正在设置 RXBW */
} BW_EditState_t;

static BW_EditState_t bw_edit_state = BW_IDLE;
static uint8_t        bw_edit_backup = 0;  /* 进入设置时保存原寄存器值 */
static int8_t         bw_edit_idx = 0;     /* BW_ENTRIES 数组索引 */

/* ================================================================
 *  SX1276 射频调试页面 (selection==3)
 *  7 槽 × 16px, 一次显示 3 槽. 全部从 X=0 开始.
 *  0:RSSI  1:AF(锁定频率)  2:BIAS(偏差)
 *  3:AFCBW  4:RXBW  5-10:小字寄存器明细(每slot 2个)
 * ================================================================ */
#define SX1276_SLOTS   11
#define SX1276_NV      3

/* 短按K3可选中的槽范围: AFCBW=3, RXBW=4 */
#define BW_SELECTABLE_SLOT_AFCBW  3
#define BW_SELECTABLE_SLOT_RXBW   4

static void bw_apply(int8_t idx, uint8_t reg_addr)
{
    SX1276_WriteReg(reg_addr, bw_regval[idx]);
}

static void draw_sx1276_page(void)
{
    int16_t rssi = SX1276_ReadRSSI();
    uint8_t ah = SX1276_ReadReg(0x15), al = SX1276_ReadReg(0x16);
    int16_t afc = (int16_t)((ah << 8) | al);
    int32_t afc_hz = ((int32_t)afc * 15625) / 256;
    uint8_t op = SX1276_ReadReg(0x01), vr = SX1276_ReadReg(0x42), ln = SX1276_ReadReg(0x0C);
    uint8_t f1 = SX1276_ReadReg(0x06), f2 = SX1276_ReadReg(0x07), f3 = SX1276_ReadReg(0x08);
    uint8_t bh = SX1276_ReadReg(0x02), bl = SX1276_ReadReg(0x03);
    uint8_t fdev_h = SX1276_ReadReg(0x04), fdev_l = SX1276_ReadReg(0x05);
    uint8_t bw = SX1276_ReadReg(0x12), pd = SX1276_ReadReg(0x1F);
    uint8_t abw = SX1276_ReadReg(0x13), rc = SX1276_ReadReg(0x0D);
    uint8_t di = SX1276_ReadReg(0x40);
    uint8_t ok = SX1276_ReadReg(0x14);

    char line[28];

    for (int8_t i = 0; i < SX1276_NV; i++) {
        int8_t slot = content_row_start + i;
        if (slot >= SX1276_SLOTS) break;
        uint8_t y = (uint8_t)(16 + i * 16);

        switch (slot) {
        case 0:
            mini_sprintf(line, "RSSI:%ddBm", rssi);
            OLED_ShowString(0, y, line, OLED_8X16);
            break;
        case 1: {
            int32_t freq_abs = 821237500 + afc_hz;
            uint32_t mhz = (uint32_t)(freq_abs / 1000000);
            uint32_t mhz_f = (uint32_t)(freq_abs % 1000000);
            mini_sprintf(line, "AF:%u.%06uMHz", mhz, mhz_f);
            OLED_ShowString(0, y, line, OLED_8X16);
            break;
        }
        case 2: {
            uint32_t abs_afc = (afc_hz >= 0) ? (uint32_t)afc_hz : (uint32_t)(-afc_hz);
            char sign = (afc_hz >= 0) ? '+' : '-';
            uint32_t khz_i = abs_afc / 1000;
            uint32_t khz_f = abs_afc % 1000;
            mini_sprintf(line, "BIAS:%c%03u.%03uKHz", sign, khz_i, khz_f);
            OLED_ShowString(0, y, line, OLED_8X16);
            break;
        }
        case 3: {
            uint8_t reg = SX1276_ReadReg(0x13);
            int8_t bidx = bw_idx_from_reg(reg);
            uint16_t bwv = bw_equiv_khz10[bidx];
            uint8_t is_sel = (slot == content_row_start + 1) && (bw_edit_state == BW_IDLE);
            uint8_t is_edit = (bw_edit_state == BW_SET_AFCBW);
            if (is_sel || is_edit) {
                OLED_ShowChar(0, y, '>', OLED_8X16);
                mini_sprintf(line, "AFCBW:%d.%dkHz",
                        (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
                if (is_edit) {
                    uint8_t el = (uint8_t)strlen(line);
                    line[el] = '*'; line[el+1] = '\0';
                }
                OLED_ShowString(8, y, line, OLED_8X16);
            } else {
                mini_sprintf(line, "AFCBW:%d.%dkHz",
                        (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
                OLED_ShowString(0, y, line, OLED_8X16);
            }
            break;
        }
        case 4: {
            uint8_t reg = SX1276_ReadReg(0x12);
            int8_t bidx = bw_idx_from_reg(reg);
            uint16_t bwv = bw_equiv_khz10[bidx];
            uint8_t is_sel = (slot == content_row_start + 1) && (bw_edit_state == BW_IDLE);
            uint8_t is_edit = (bw_edit_state == BW_SET_RXBW);
            if (is_sel || is_edit) {
                OLED_ShowChar(0, y, '>', OLED_8X16);
                mini_sprintf(line, "RXBW:%d.%dkHz",
                        (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
                if (is_edit) {
                    uint8_t el = (uint8_t)strlen(line);
                    line[el] = '*'; line[el+1] = '\0';
                }
                OLED_ShowString(8, y, line, OLED_8X16);
            } else {
                mini_sprintf(line, "RXBW:%d.%dkHz",
                        (uint16_t)(bwv / 10), (uint16_t)(bwv % 10));
                OLED_ShowString(0, y, line, OLED_8X16);
            }
            break;
        }
        case 5:
            mini_sprintf(line, "OpMode:%02X", op);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "Version:%02X", vr);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
            break;
        case 6:
            mini_sprintf(line, "LNAGain:%02X", ln);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "Frequency:%02X%02X%02X", f1, f2, f3);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
            break;
        case 7:
            mini_sprintf(line, "BitRate:%02X%02X", bh, bl);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "FreqDeviation:%02X%02X", fdev_h, fdev_l);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
            break;
        case 8:
            mini_sprintf(line, "RXBandwidth:%02X", bw);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "AFCBandwidth:%02X", abw);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
            break;
        case 9:
            mini_sprintf(line, "PreambleDetect:%02X", pd);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "RxConfig:%02X", rc);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
            break;
        case 10:
            mini_sprintf(line, "OOKPeak:%02X", ok);
            OLED_ShowString(0, y,     line, OLED_6X8);
            mini_sprintf(line, "DIOMapping:%02X", di);
            OLED_ShowString(0, y + 8, line, OLED_6X8);
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

/* 信号检测内页: 5 数据行, 每次显示 3 行 */
#define SIG_DET_SLOTS   5
#define SIG_DET_NV      3

static void draw_signal_detect_page(void)
{
    const LBJ_Message_t *msg = LBJ_GetLatest();
    if (!msg || !msg->has_train_id) {
        OLED_ShowGBString(8, 24, (const uint8_t *)"\xB5\xC8\xB4\xFD\xD0\xC5\xBA\xC5\x2E\x2E\x2E", OLED_8X16);
        return;
    }

    for (int8_t i = 0; i < SIG_DET_NV; i++) {
        int8_t slot = content_row_start + i;
        if (slot >= SIG_DET_SLOTS) break;
        uint8_t y = (uint8_t)(16 + i * 16);
        uint8_t buf[64];
        uint8_t p = 0;

        switch (slot) {
        case 0: /* 车次 */
            memcpy(buf + p, gb_l_cs, 4); p += 4;
            buf[p++] = ':'; buf[p++] = ' ';
            {
                uint8_t tl = (uint8_t)strlen(msg->train_id);
                if (tl > 30) tl = 30;
                memcpy(buf + p, msg->train_id, tl); p += tl;
            }
            break;

        case 1: /* 速度 */
            memcpy(buf + p, gb_l_sd, 4); p += 4;
            if (msg->has_speed)
                p += (uint8_t)mini_sprintf((char *)(buf + p), ": %d km/h", msg->speed);
            else {
                memcpy(buf + p, ": ---", 5); p += 5;
            }
            break;

        case 2: /* 公里 */
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
            break;

        case 3: /* 机车 */
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
            break;

        case 4: /* 线路 */
            memcpy(buf + p, gb_l_xl, 4); p += 4;
            buf[p++] = ':'; buf[p++] = ' ';
            if (msg->is_extended && msg->route_gbk_len > 0 && msg->route_gbk_len < 30) {
                memcpy(buf + p, msg->route_gbk, msg->route_gbk_len);
                p += msg->route_gbk_len;
            } else {
                memcpy(buf + p, "----", 4); p += 4;
            }
            break;
        }

        buf[p] = '\0';
        OLED_ShowGBString(8, y, buf, OLED_8X16);
    }
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
 * 内页滚动辅助
 *===========================================================================*/
static void page_scroll_up(void) {
    if (bw_edit_state != BW_IDLE) {
        int8_t new_idx = bw_edit_idx + 1;
        if (new_idx < BW_ENTRIES) {
            bw_edit_idx = new_idx;
            uint8_t reg_addr = (bw_edit_state == BW_SET_AFCBW) ? 0x13 : 0x12;
            bw_apply(bw_edit_idx, reg_addr);
            needs_redraw = 1;
        }
        return;
    }
    if (content_row_start > 0) { content_row_start--; needs_redraw = 1; }
}
static void page_scroll_down(void) {
    if (bw_edit_state != BW_IDLE) {
        int8_t new_idx = bw_edit_idx - 1;
        if (new_idx >= 0) {
            bw_edit_idx = new_idx;
            uint8_t reg_addr = (bw_edit_state == BW_SET_AFCBW) ? 0x13 : 0x12;
            bw_apply(bw_edit_idx, reg_addr);
            needs_redraw = 1;
        }
        return;
    }
    if (selection == 1) {
        if (content_row_start < SIG_DET_SLOTS - SIG_DET_NV) { content_row_start++; needs_redraw = 1; }
    } else if (selection == 3) {
        if (content_row_start < SX1276_SLOTS - SX1276_NV) { content_row_start++; needs_redraw = 1; }
    } else {
        uint8_t total = 0; char r[MAX_CONTENT_ROWS][CONTENT_LEN];
        if (page_callbacks[selection]) page_callbacks[selection](r, &total);
        if ((int16_t)content_row_start <= (int16_t)total - 3) { content_row_start++; needs_redraw = 1; }
    }
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
        if (k1) {
            if (!prev_k1) { k1_press_ms = now; k1_last_rep = now; page_scroll_up(); }
            else if (now - k1_press_ms >= KEY_REPEAT_DELAY_MS && now - k1_last_rep >= KEY_REPEAT_RATE_MS)
                { k1_last_rep = now; page_scroll_up(); }
        }
        if (k2) {
            if (!prev_k2) { k2_press_ms = now; k2_last_rep = now; page_scroll_down(); }
            else if (now - k2_press_ms >= KEY_REPEAT_DELAY_MS && now - k2_last_rep >= KEY_REPEAT_RATE_MS)
                { k2_last_rep = now; page_scroll_down(); }
        }
    }
    prev_k1 = k1; prev_k2 = k2;

    /* ================================================================
     * 射频调试页 BW 编辑模式的 K3 处理
     * ================================================================ */
    if (selection == 3 && menu_state == MENU_PAGE) {

        /* 在编辑状态下 */
        if (bw_edit_state != BW_IDLE) {
            if (k3 && !prev_k3) {
                k3_press_ms = now;
                k3_suppress_release = 0;
            }
            if (k3 && prev_k3 && now - k3_press_ms >= KEY_LONG_PRESS_MS && k3_suppress_release == 0) {
                /* 长按: 不保存, 恢复备份 */
                uint8_t reg_addr = (bw_edit_state == BW_SET_AFCBW) ? 0x13 : 0x12;
                SX1276_WriteReg(reg_addr, bw_edit_backup);
                bw_edit_state = BW_IDLE;
                needs_redraw = 1;
                k3_suppress_release = 2;
                BLE_SendString((const uint8_t *)"[BW] cancel\r\n");
                prev_k3 = k3;
                return;
            }
            if (!k3 && prev_k3 && k3_suppress_release == 0) {
                /* 短按释放: 保存退出 */
                SX1276_SaveBwConfig();
                bw_edit_state = BW_IDLE;
                needs_redraw = 1;
                k3_suppress_release = 1;
                BLE_SendString((const uint8_t *)"[BW] saved\r\n");
                prev_k3 = k3;
                return;
            }
            if (!k3 && prev_k3 && k3_suppress_release == 2) {
                k3_suppress_release = 0;
            }
            prev_k3 = k3;
            return;
        }

        /* 在空闲状态下, 选中行是 AFCBW 或 RXBW 时短按 K3 进入编辑 */
        if (k3 && !prev_k3 && menu_state == MENU_PAGE) {
            int8_t sel_slot = content_row_start + 1;  /* 大字第3行 */
            if (sel_slot == BW_SELECTABLE_SLOT_AFCBW) {
                bw_edit_state = BW_SET_AFCBW;
                bw_edit_backup = SX1276_ReadReg(0x13);
                bw_edit_idx = bw_idx_from_reg(bw_edit_backup);
                content_row_start = BW_SELECTABLE_SLOT_AFCBW - 1;
                k3_press_ms = now;
                k3_suppress_release = 1;
                needs_redraw = 1;
                BLE_SendString((const uint8_t *)"[BW] enter AFCBW\r\n");
            } else if (sel_slot == BW_SELECTABLE_SLOT_RXBW) {
                bw_edit_state = BW_SET_RXBW;
                bw_edit_backup = SX1276_ReadReg(0x12);
                bw_edit_idx = bw_idx_from_reg(bw_edit_backup);
                content_row_start = BW_SELECTABLE_SLOT_RXBW - 1;
                k3_press_ms = now;
                k3_suppress_release = 1;
                needs_redraw = 1;
                BLE_SendString((const uint8_t *)"[BW] enter RXBW\r\n");
            }
        }
    }

    /* 非 BW 编辑: 标准的 K3 菜单切换.
       仅在 MENU_PAGE 且不是射频调试页时才响应长按返回菜单列表 */
    if (!(selection == 3 && menu_state == MENU_PAGE)) {
        if (k3 && !prev_k3) { k3_press_ms = now; k3_suppress_release = 0; }
        if (k3 && prev_k3 && menu_state == MENU_PAGE && now - k3_press_ms >= KEY_LONG_PRESS_MS) {
            menu_state = MENU_LIST; needs_redraw = 1; k3_suppress_release = 1;
        }
        if (!k3 && prev_k3 && !k3_suppress_release && menu_state == MENU_LIST) {
            menu_state = MENU_PAGE; needs_redraw = 1; content_row_start = 0;
        }
    } else {
        /* 射频调试页: 只有不在 BW 编辑状态时才允许长按返回 */
        if (bw_edit_state == BW_IDLE) {
            if (k3 && !prev_k3) { k3_press_ms = now; k3_suppress_release = 0; }
            if (k3 && prev_k3 && menu_state == MENU_PAGE && now - k3_press_ms >= KEY_LONG_PRESS_MS) {
                menu_state = MENU_LIST; needs_redraw = 1; k3_suppress_release = 1;
            }
        }
        if (!k3 && prev_k3 && !k3_suppress_release && menu_state == MENU_LIST) {
            menu_state = MENU_PAGE; needs_redraw = 1; content_row_start = 0;
        }
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
    bw_edit_state = BW_IDLE;
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
            OLED_ShowGBString(48, 16, (const uint8_t *)"\xC4\xE3\xBA\xC3", OLED_8X16);
            OLED_ShowGBString(0, 32, (const uint8_t *)"\xBD\xBB\xCD\xA8\xC7\xBF\xB9\xFA\xCC\xFA\xC2\xB7\xCF\xC8\xD0\xD0", OLED_8X16);
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

            /* 编辑状态下固定重绘 */
            if (selection == 3 && bw_edit_state != BW_IDLE) { draw_page(); break; }

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
