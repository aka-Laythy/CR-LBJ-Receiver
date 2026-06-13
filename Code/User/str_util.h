#ifndef __STR_UTIL_H
#define __STR_UTIL_H

#include <stdint.h>
#include <stdarg.h>

/* ================================================================
 *  轻量字符串工具 — 替代 stdio.h 的 sprintf 家族
 *
 *  动机:
 *    stdio.h / libc 的 sprintf 内部维护了静态 FILE 结构体缓冲区
 *    (stdout 的 BUFSIZ 通常 256~1024B), 这些全部分配在 .bss 里。
 *    本项目 MCU (CH32V005F6P6) 仅 6KB SRAM, BSS 已接近上限,
 *    移除 stdio 可释放约 500~1000 字节。
 *
 *    mini_sprintf 仅支持本工程实际使用的格式说明符:
 *      %d      — 有符号整数
 *      %02d    — 有符号整数, 宽度2, 高位补0
 *      %+05d   — 有符号整数, 宽度5, 高位补0, 正数前加 '+'
 *      %u      — 无符号整数
 *      %02u    — 无符号整数, 宽度2, 高位补0
 *      %04u    — 无符号整数, 宽度4, 高位补0
 *      %lu     — 无符号 32 位整数 (long 在 RV32 上即 uint32_t)
 *      %02X    — 十六进制, 宽度2, 高位补0
 *      %c      — 单个字符
 *  不支持: float/string/pointer, 不需要也不实现。
 *
 *    vsprintf 一并由 mini_vsprintf 替代, 供 OLED_Printf 使用。
 * ================================================================ */

/* 16-bit 无符号整型转 ASCII, 返回写入字符数 */
uint8_t utoa16(char *out, uint16_t val);

/* 16-bit 有符号整型转 ASCII (含负号), 返回写入字符数 */
uint8_t itoa16(char *out, int16_t val);

/* 32-bit 无符号整型转十六进制, 返回写入字符数。
 * digits: 最少位数 (不足高位补 '0') */
uint8_t utox32(char *out, uint32_t val, uint8_t digits);

/* 32-bit 无符号 → 十进制, 宽度不足补填充字符 */
uint8_t utoa32_pad(char *out, uint32_t val, uint8_t width, char pad);

/* 32-bit 有符号 → 十进制, 可选正号前缀 */
uint8_t itoa32_pad(char *out, int32_t val, uint8_t width, char pad, uint8_t plus);

/* 轻量 sprintf 替代, 返回写入字符数 (不含 '\0') */
uint8_t mini_sprintf(char *out, const char *fmt, ...);

/* 轻量 vsprintf 替代 */
uint8_t mini_vsprintf(char *out, const char *fmt, va_list args);

/* 轻量 atou32 — 十进制字符串 → uint32_t, 遇非数字停止 */
uint32_t mini_atou32(const char *s);

/* ================================================================
 *  mini_atan2 — 替代 math.h 的 atan2
 *
 *  动机:
 *    math.h 的 atan2() 是标准 IEEE754 双精度实现, 在无硬件 FPU
 *    的 CH32V005(RV32EC) 上依赖软浮点库 (__adddf3, __muldf3 等),
 *    体积巨大 (代码 + .rodata 可达数 KB)。
 *
 *    OLED 画圆弧时只需要粗略的角度判断 (误差 <2° 即可),
 *    因此用 65 项查表实现整数 atan2, 精度约 ±1.5°, 零浮点依赖。
 *
 *    查表原理:
 *      atan_tab[i] = atan(i / 64) * 180 / PI   (i = 0..64)
 *      即 0° ~ 45° 的 atan 预计算值, 步进约 0.7°。
 *      角度 > 45° 时用 atan(90°-α) = 90° - atan(α) 对称.
 *      象限通过参数符号判断.
 *
 *  参数: y, x — 直角坐标 (相对圆心)
 *  返回: 角度, -180 ~ 180 度
 * ================================================================ */
int16_t mini_atan2(int16_t y, int16_t x);

#endif
