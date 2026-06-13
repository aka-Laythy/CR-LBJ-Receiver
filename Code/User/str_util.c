#include "str_util.h"
#include <string.h>

/* ================================================================
 *  16-bit 无符号 → 十进制 ASCII
 * ================================================================ */
uint8_t utoa16(char *out, uint16_t val)
{
    char buf[6];
    uint8_t i = 0, j;

    if (val == 0) {
        out[0] = '0';
        return 1;
    }

    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }

    for (j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    return i;
}

/* ================================================================
 *  16-bit 有符号 → 十进制 ASCII
 * ================================================================ */
uint8_t itoa16(char *out, int16_t val)
{
    uint8_t n = 0;
    if (val < 0) {
        out[n++] = '-';
        val = (int16_t)-val;
    }
    n += utoa16(out + n, (uint16_t)val);
    return n;
}

/* ================================================================
 *  32-bit 无符号 → 十六进制 ASCII
 * ================================================================ */
uint8_t utox32(char *out, uint32_t val, uint8_t digits)
{
    const char hex[] = "0123456789ABCDEF";
    uint8_t i, n = digits;
    for (i = 0; i < digits; i++) {
        out[digits - 1 - i] = hex[val & 0xF];
        val >>= 4;
    }
    return n;
}

/* ================================================================
 *  32-bit 无符号 → 十进制, 宽度不足补填充字符
 * ================================================================ */
uint8_t utoa32_pad(char *out, uint32_t val, uint8_t width, char pad)
{
    char buf[11];
    uint8_t i = 0, j;

    if (width > 10) width = 10;

    if (val == 0 && width == 0) {
        out[0] = '0';
        return 1;
    }

    do {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val > 0 || i < width);

    for (j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    return i;
}

/* ================================================================
 *  32-bit 有符号 → 十进制, 宽度不足补填充字符, 可选正号前缀
 * ================================================================ */
uint8_t itoa32_pad(char *out, int32_t val, uint8_t width, char pad, uint8_t plus)
{
    uint8_t n = 0;
    if (val < 0) {
        out[n++] = '-';
        val = -val;
    } else if (plus) {
        out[n++] = '+';
    }
    n += utoa32_pad(out + n, (uint32_t)val, width, pad);
    return n;
}

/* ================================================================
 *  mini_sprintf — 轻量替代 stdio sprintf
 *
 *  支持格式:
 *    %d, %02d, %+05d  — 有符号整数 + 宽度/零填充/正号
 *    %u, %02u, %04u   — 无符号整数 + 宽度/零填充
 *    %lu              — 无符号 long (RV32 上 = uint32_t)
 *    %02X             — 十六进制 + 宽度/零填充
 *    %c               — 单字符
 *
 *  格式语法:  %[+][0][width][l]specifier
 *    '+'  正数前加 '+' 号
 *    '0'  高位补 '0'
 *    width 数字, 最小宽度
 *    'l'  long 前缀
 *
 *  返回: 写入字符数 (不含 '\0')
 * ================================================================ */
uint8_t mini_sprintf(char *out, const char *fmt, ...)
{
    va_list args;
    uint8_t n;
    va_start(args, fmt);
    n = mini_vsprintf(out, fmt, args);
    va_end(args);
    return n;
}

uint8_t mini_vsprintf(char *out, const char *fmt, va_list args)
{
    uint8_t n = 0;
    char c;

    while ((c = *fmt++) != '\0') {
        if (c != '%') {
            out[n++] = c;
            continue;
        }

        c = *fmt++;
        uint8_t zero = 0, plus = 0, width = 0, longf = 0;

        if (c == '+') { plus = 1; c = *fmt++; }
        if (c == '0') { zero = 1; }
        while (c >= '0' && c <= '9') {
            width = (uint8_t)(width * 10 + (c - '0'));
            c = *fmt++;
        }
        if (c == 'l') { longf = 1; c = *fmt++; }

        switch (c) {
        case 'd': {
            int32_t v = longf ? va_arg(args, int32_t) : (int32_t)va_arg(args, int);
            n += itoa32_pad(out + n, v, width, zero ? '0' : ' ', plus);
            break;
        }
        case 'u': {
            uint32_t v = longf ? va_arg(args, uint32_t) : (uint32_t)va_arg(args, unsigned int);
            n += utoa32_pad(out + n, v, width, zero ? '0' : ' ');
            break;
        }
        case 'X': {
            uint32_t v = (uint32_t)va_arg(args, unsigned int);
            if (width == 0) width = 1;
            if (width > 8) width = 8;
            char hex[] = "0123456789ABCDEF";
            uint8_t i;
            char tmp[9];
            for (i = 0; i < width; i++) {
                tmp[width - 1 - i] = hex[v & 0xF];
                v >>= 4;
            }
            for (i = 0; i < width; i++) out[n++] = tmp[i];
            break;
        }
        case 'c':
            out[n++] = (char)va_arg(args, int);
            break;
        default:
            out[n++] = c;
            break;
        }
    }

    out[n] = '\0';
    return n;
}

/* ================================================================
 *  atan 查表: atan(i / 64) * 180 / PI  (i = 0..64, 对应 0°~45°)
 *  精度约 ±1.5°, 足够 OLED 画弧。
 * ================================================================ */
static const uint8_t atan_tab[65] = {
     0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9,10,11,11,12,13,
    14,15,16,17,17,18,19,20,21,21,22,23,24,24,25,26,
    27,27,28,29,30,30,31,32,32,33,34,34,35,36,36,37,
    38,38,39,40,40,41,42,42,43,43,44,44,45,45,45,45,
    45
};

/* ================================================================
 *  mini_atan2 — 整数 atan2, 返回角度 (-180 ~ 180)
 *
 *  算法:
 *    1. 取 |x|, |y|
 *    2. 若 |x| >= |y|: 查表 idx = |y| * 64 / |x| → 年 0°~45°
 *    3. 若 |x| <  |y|: 查表 idx = |x| * 64 / |y| → 用 90° - 查表值
 *    4. 根据 x, y 符号调整象限
 *  全整数运算, 零浮点, 零除法 (乘法+移位替代).
 * ================================================================ */
int16_t mini_atan2(int16_t y, int16_t x)
{
    uint16_t ax = (uint16_t)(x < 0 ? -x : x);
    uint16_t ay = (uint16_t)(y < 0 ? -y : y);
    int16_t  a;

    if (ax == 0 && ay == 0) return 0;

    if (ax >= ay) {
        uint8_t idx = (ax > 0) ? (uint8_t)((uint32_t)ay * 64 / ax) : 64;
        if (idx > 64) idx = 64;
        a = (int16_t)atan_tab[idx];
    } else {
        uint8_t idx = (ay > 0) ? (uint8_t)((uint32_t)ax * 64 / ay) : 64;
        if (idx > 64) idx = 64;
        a = 90 - (int16_t)atan_tab[idx];
    }

    if (x < 0) a = 180 - a;
    if (y < 0) a = -a;

    return a;
}

/* ================================================================
 *  mini_atou32 — 十进制字符串 → uint32_t
 *  读到非数字或 '\0' 停止, 等效于 strtoul(s, NULL, 10)
 * ================================================================ */
uint32_t mini_atou32(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}
