#ifndef __STR_UTIL_H
#define __STR_UTIL_H

#include <stdint.h>

/* 无 heap / 无 float 的字符串工具 */

/* 16-bit 无符号整型转 ASCII，返回写入字符数 */
uint8_t utoa16(char *out, uint16_t val);

/* 16-bit 有符号整型转 ASCII (含负号)，返回写入字符数 */
uint8_t itoa16(char *out, int16_t val);

/* 32-bit 无符号整型转十六进制，返回写入字符数。
 * digits: 最少位数 (不足高位补 '0') */
uint8_t utox32(char *out, uint32_t val, uint8_t digits);

#endif
