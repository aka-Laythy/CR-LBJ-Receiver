#include "lbj.h"
#include <string.h>

/* ================================================================
 *  LBJ 解析器（依据 TB/T 3504-2018 第 9.1.2 节）
 *
 *  消息格式: [5C 车次号][3C 速度][5C 公里标] = 13 BCD 字符
 *
 *  POCSAG 地址:
 *    1234000 = 列车接近预警
 *    1234008 = 时钟校准
 *
 *  功能位:
 *    1 (01b) = 车次号为奇数
 *    3 (11b) = 车次号为偶数
 * ================================================================ */

#define LBJ_RIC_APPROACH  1234000UL
#define LBJ_RIC_CLOCK     1234008UL

static LBJ_Callback_t g_lbj_cb = 0;

/* ================================================================
 *  提取纯数字子串，去除空格和负号
 * ================================================================ */
static bool extract_digits(const char *src, uint8_t start, uint8_t len,
                            char *out, uint8_t out_size)
{
    uint8_t j = 0;
    for (uint8_t i = 0; i < len && j < out_size - 1; i++) {
        char c = src[start + i];
        if (c >= '0' && c <= '9') {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return j > 0;
}

static int16_t chars_to_int(const char *s, uint8_t len)
{
    int16_t val = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
        }
    }
    return val;
}

/* ================================================================
 *  判断字段是否全部为 '-' (无效数据标记)
 *  TB/T: 数据无效时填充全 D (负号)
 * ================================================================ */
static bool is_all_invalid(const char *s, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        if (s[i] != '-') return false;
    }
    return true;
}

/* ================================================================
 *  主解析函数
 * ================================================================ */
void LBJ_ParsePOCSAG(uint32_t ric, uint8_t function,
                      const char *text, uint8_t len,
                      int16_t rssi)
{
    LBJ_Message_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.ric      = ric;
    msg.function = function;
    msg.rssi_dbm = rssi;

    if (len > 255) len = 255;
    memcpy(msg.raw_text, text, len);
    msg.raw_text[len] = '\0';
    msg.raw_len = len;

    /* 只处理列车接近预警 RIC (1234000) */
    if (ric != LBJ_RIC_APPROACH || len < 13) {
        if (g_lbj_cb) g_lbj_cb(&msg);
        return;
    }

    /* --------------- 车次号: 位置 0..4, 共 5 字符 --------------- */
    msg.has_train_id = !is_all_invalid(text + 0, 5);
    if (msg.has_train_id) {
        extract_digits(text, 0, 5, msg.train_id, sizeof(msg.train_id));
        /* 功能位: 1=奇数, 3=偶数 */
        msg.train_odd_even = (function == 3);
    }

    /* --------------- 速度: 位置 5..7, 共 3 字符 --------------- */
    msg.has_speed = !is_all_invalid(text + 5, 3);
    if (msg.has_speed) {
        char tmp[4] = {0};
        extract_digits(text, 5, 3, tmp, sizeof(tmp));
        msg.speed = chars_to_int(tmp, 3);
    }

    /* --------------- 公里标: 位置 8..12, 共 5 字符 --------------- */
    msg.has_km = !is_all_invalid(text + 8, 5);
    if (msg.has_km) {
        msg.km_negative = (text[8] == '-');
        extract_digits(text, 8, 5, msg.km_post, sizeof(msg.km_post));
    }

    if (g_lbj_cb) {
        g_lbj_cb(&msg);
    }
}

void LBJ_Init(LBJ_Callback_t cb)
{
    g_lbj_cb = cb;
}
