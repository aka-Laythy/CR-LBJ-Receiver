/* gps.h */
#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ch32v30x.h"

// uart rx ring buffer, 1024 bytes is enough
#define GPS_RX_BUF_SIZE     1024
// GPS NEMA 完整一帧
#define GPS_FRAME_BUF_SIZE  1024

// 经纬度结构体
typedef struct {
    double latitude;      // 纬度，度格式（如29.345612）
    char lat_dir;         // 'N' 或 'S'
    double longitude;     // 经度，度格式（如104.712345）
    char lon_dir;         // 'E' 或 'W'
    bool valid;           // 是否有效定位
} GPS_Coord_TypeDef;

// 时间结构体，除了时间戳是UTC时间，其他均为北京时间
typedef struct {
    uint16_t year;        // 年（如2026）
    uint8_t month;        // 月（1-12）
    uint8_t day;          // 日（1-31）
    uint8_t hour;         // 时（北京时间0-23）
    uint8_t minute;       // 分（0-59）
    uint8_t second;       // 秒（0-59）
    uint32_t timestamp;   // UTC时间戳（秒）
    bool valid;           // 是否有效
} GPS_Time_TypeDef;

void GPS_Init(void);
void GPS_CheckNewFrame(void);           
bool GPS_GetLatestFrame(u8 *buf, uint16_t *len);

bool GPS_GetCoord(void);        // 获取经纬度
bool GPS_GetTime(void);           // 获取北京时间

#endif