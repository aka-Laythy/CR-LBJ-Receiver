#include "str_util.h"

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
