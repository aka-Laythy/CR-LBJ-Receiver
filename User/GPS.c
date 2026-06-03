#include "GPS.h"
#include <string.h>
#include <stdlib.h>

extern volatile uint32_t tick_ms;

/*=============================================================================
 * 环形缓冲区数据结构
 *===========================================================================*/
typedef struct {
    volatile uint8_t  buf[GPS_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
} RingBuf_t;

static volatile RingBuf_t gps_rx_ring = {0};
static volatile RingBuf_t gps_tx_ring = {0};
static volatile uint8_t   gps_tx_busy = 0;

/*=============================================================================
 * 全局行缓冲区：用于 NMEA 行读取
 *===========================================================================*/
#define GPS_LINE_BUF_SIZE  512
static volatile char    gps_line_buf[GPS_LINE_BUF_SIZE];
static volatile uint8_t gps_line_idx = 0;
static volatile bool    gps_line_ready = false;

/*=============================================================================
 * 全局缓冲器：保存上一次有效数据
 *===========================================================================*/
static volatile GPS_Data_TypeDef gps_data_buffer = {0};
static volatile bool             gps_data_valid   = false;
static volatile uint32_t         gps_last_rx_time = 0;

/*=============================================================================
 * 内部静态函数：环形缓冲区操作
 *===========================================================================*/
static inline uint8_t __ring_push(volatile RingBuf_t *r, uint8_t byte, uint16_t size)
{
    if (r->count >= size) {
        return 0;   /* 缓冲区满 */
    }
    r->buf[r->head] = byte;
    r->head = (r->head + 1) % size;
    r->count++;
    return 1;
}

static inline uint8_t __ring_pop(volatile RingBuf_t *r, uint8_t *byte, uint16_t size)
{
    if (r->count == 0) {
        return 0;   /* 缓冲区空 */
    }
    *byte = r->buf[r->tail];
    r->tail = (r->tail + 1) % size;
    r->count--;
    return 1;
}

/*=============================================================================
 * @brief   GPS / USART1 初始化
 * @note    配置 PD5(TX)、PD6(RX) 为 USART1 复用功能，115200-8-N1
 *          并使能 RXNE 中断。
 *===========================================================================*/
void GPS_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};

    /* 使能 USART1、GPIOD 时钟 */
    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_USART1, ENABLE);

    /* PD5 TX — 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* PD6 RX — 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* USART1 参数：115200，8 数据位，1 停止位，无校验，无流控 */
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    /* NVIC 配置：USART1 全局中断 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能接收中断并启动 USART1 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
}

/*=============================================================================
 * @brief   发送单字节（非阻塞）
 * @return  1=成功写入发送缓冲区，0=发送缓冲区满
 *===========================================================================*/
uint8_t GPS_SendByte(uint8_t byte)
{
    uint8_t ret;

    __disable_irq();
    ret = __ring_push(&gps_tx_ring, byte, GPS_TX_BUF_SIZE);

    if (ret && !gps_tx_busy) {
        gps_tx_busy = 1;
        USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
    }
    __enable_irq();

    return ret;
}

/*=============================================================================
 * @brief   发送数据块（非阻塞）
 * @return  1=全部写入成功，0=中途缓冲区满（已发送部分数据）
 *===========================================================================*/
uint8_t GPS_SendData(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (!GPS_SendByte(data[i])) {
            return 0;
        }
    }
    return 1;
}

/*=============================================================================
 * @brief   发送以 '\0' 结尾的字符串（非阻塞）
 * @return  1=全部写入成功，0=中途缓冲区满
 *===========================================================================*/
uint8_t GPS_SendString(const uint8_t *str)
{
    while (*str) {
        if (!GPS_SendByte(*str++)) {
            return 0;
        }
    }
    return 1;
}

/*=============================================================================
 * @brief   从接收缓冲区读取单字节
 * @return  1=读取成功，0=接收缓冲区空
 *===========================================================================*/
uint8_t GPS_ReadByte(uint8_t *byte)
{
    uint8_t ret;
    __disable_irq();
    ret = __ring_pop(&gps_rx_ring, byte, GPS_RX_BUF_SIZE);
    __enable_irq();
    return ret;
}

/*=============================================================================
 * @brief   从接收缓冲区批量读取
 * @return  实际读取到的字节数
 *===========================================================================*/
uint16_t GPS_ReadData(uint8_t *buf, uint16_t len)
{
    uint16_t i = 0;
    __disable_irq();
    while (i < len && __ring_pop(&gps_rx_ring, &buf[i], GPS_RX_BUF_SIZE)) {
        i++;
    }
    __enable_irq();
    return i;
}

/*=============================================================================
 * @brief   获取接收缓冲区当前已缓存字节数
 *===========================================================================*/
uint16_t GPS_GetRxCount(void)
{
    uint16_t ret;
    __disable_irq();
    ret = gps_rx_ring.count;
    __enable_irq();
    return ret;
}

/*=============================================================================
 * @brief   清空接收缓冲区
 *===========================================================================*/
void GPS_ClearRx(void)
{
    __disable_irq();
    gps_rx_ring.head  = 0;
    gps_rx_ring.tail  = 0;
    gps_rx_ring.count = 0;
    __enable_irq();
}

/*=============================================================================
 * @brief   查询发送是否忙
 * @return  1=正在发送，0=空闲
 *===========================================================================*/
uint8_t GPS_IsTxBusy(void)
{
    return gps_tx_busy;
}

/*=============================================================================
 * 内部辅助函数：解析 GGA 语句
 * 格式：$GPGGA,time,lat,N,lon,E,quality,numSats,HDOP,alt,M,sep,M,,*cs
 *===========================================================================*/
static bool parse_GGA(const char *buf, GPS_Data_TypeDef *data)
{
    // 查找 $GPGGA 或 $GNGGA
    const char *gga = strstr(buf, "$GPGGA");
    if (!gga) gga = strstr(buf, "$GNGGA");
    if (!gga) return false;

    // 找到行尾
    const char *line_end = strchr(gga, '\n');
    if (!line_end) line_end = strchr(gga, '\r');
    if (!line_end) line_end = gga + strlen(gga);

    // 临时截断
    char save_char = *line_end;
    *((char*)line_end) = '\0';

    // 手动解析字段
    const char *p = gga;
    int comma_count = 0;
    char lat_str[16] = {0};
    char lon_str[16] = {0};
    char lat_dir = 0;
    char lon_dir = 0;
    char fix_quality = '0';

    while (*p && p < line_end) {
        if (*p == ',') {
            comma_count++;
            p++;
            const char *field_start = p;
            int field_len = 0;
            while (*p && *p != ',' && *p != '*') {
                p++;
                field_len++;
            }
            switch (comma_count) {
                case 2: // 纬度 ddmm.mmmm
                    if (field_len > 0 && field_len < 16) {
                        memcpy(lat_str, field_start, field_len);
                        lat_str[field_len] = '\0';
                    }
                    break;
                case 3: // N/S
                    if (field_len > 0) lat_dir = *field_start;
                    break;
                case 4: // 经度 dddmm.mmmm
                    if (field_len > 0 && field_len < 16) {
                        memcpy(lon_str, field_start, field_len);
                        lon_str[field_len] = '\0';
                    }
                    break;
                case 5: // E/W
                    if (field_len > 0) lon_dir = *field_start;
                    break;
                case 6: // 定位质量
                    if (field_len > 0) fix_quality = *field_start;
                    break;
            }
        } else {
            p++;
        }
    }

    // 恢复
    *((char*)line_end) = save_char;

    // 验证
    if (lat_str[0] == 0 || lon_str[0] == 0 ||
        (lat_dir != 'N' && lat_dir != 'S') ||
        (lon_dir != 'E' && lon_dir != 'W')) {
        return false;
    }

    // 转换纬度
    double lat_val = strtod(lat_str, NULL);
    int lat_deg = (int)(lat_val / 100);
    double lat_min = lat_val - lat_deg * 100;
    data->latitude = lat_deg + lat_min / 60.0;
    data->lat_dir = lat_dir;

    // 转换经度
    double lon_val = strtod(lon_str, NULL);
    int lon_deg = (int)(lon_val / 100);
    double lon_min = lon_val - lon_deg * 100;
    data->longitude = lon_deg + lon_min / 60.0;
    data->lon_dir = lon_dir;

    // 定位质量 0=无效, 1=GPS, 2=DGPS
    data->valid = (fix_quality >= '1');

    return data->valid;
}

/*=============================================================================
 * 内部辅助函数：解析 RMC 语句
 * 格式：$GPRMC,time,status,lat,N,lon,E,speed,course,date,magvar,magdir,*cs
 *===========================================================================*/
static bool parse_RMC(const char *buf, GPS_Data_TypeDef *data)
{
    // 查找 $GPRMC 或 $GNRMC
    const char *rmc = strstr(buf, "$GPRMC");
    if (!rmc) rmc = strstr(buf, "$GNRMC");
    if (!rmc) return false;

    // 找到行尾
    const char *line_end = strchr(rmc, '\n');
    if (!line_end) line_end = strchr(rmc, '\r');
    if (!line_end) line_end = rmc + strlen(rmc);

    // 临时截断
    char save_char = *line_end;
    *((char*)line_end) = '\0';

    // 手动解析字段
    const char *p = rmc;
    int comma_count = 0;
    char time_str[16] = {0};
    char status = 0;
    char lat_str[16] = {0};
    char lon_str[16] = {0};
    char lat_dir = 0;
    char lon_dir = 0;
    char date_str[16] = {0};

    while (*p && p < line_end) {
        if (*p == ',') {
            comma_count++;
            p++;
            const char *field_start = p;
            int field_len = 0;
            while (*p && *p != ',' && *p != '*') {
                p++;
                field_len++;
            }
            switch (comma_count) {
                case 1: // UTC 时间 hhmmss.ss
                    if (field_len >= 6 && field_len < 16) {
                        memcpy(time_str, field_start, field_len);
                        time_str[field_len] = '\0';
                    }
                    break;
                case 2: // 状态 A=有效, V=无效
                    if (field_len > 0) status = *field_start;
                    break;
                case 3: // 纬度 ddmm.mmmm
                    if (field_len > 0 && field_len < 16) {
                        memcpy(lat_str, field_start, field_len);
                        lat_str[field_len] = '\0';
                    }
                    break;
                case 4: // N/S
                    if (field_len > 0) lat_dir = *field_start;
                    break;
                case 5: // 经度 dddmm.mmmm
                    if (field_len > 0 && field_len < 16) {
                        memcpy(lon_str, field_start, field_len);
                        lon_str[field_len] = '\0';
                    }
                    break;
                case 6: // E/W
                    if (field_len > 0) lon_dir = *field_start;
                    break;
                case 9: // 日期 ddmmyy
                    if (field_len >= 6 && field_len < 16) {
                        memcpy(date_str, field_start, field_len);
                        date_str[field_len] = '\0';
                    }
                    break;
            }
        } else {
            p++;
        }
    }

    // 恢复
    *((char*)line_end) = save_char;

    // 验证
    if (time_str[0] == 0 || date_str[0] == 0 ||
        lat_str[0] == 0 || lon_str[0] == 0 ||
        (lat_dir != 'N' && lat_dir != 'S') ||
        (lon_dir != 'E' && lon_dir != 'W')) {
        return false;
    }

    // 解析 UTC 时间
    int utc_hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int utc_min  = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    int utc_sec  = (time_str[4] - '0') * 10 + (time_str[5] - '0');

    // 解析日期
    int day   = (date_str[0] - '0') * 10 + (date_str[1] - '0');
    int month = (date_str[2] - '0') * 10 + (date_str[3] - '0');
    int year  = 2000 + (date_str[4] - '0') * 10 + (date_str[5] - '0');

    // 转换为北京时间（UTC+8）
    int beijing_hour = utc_hour + 8;
    int beijing_day = day;
    int beijing_month = month;
    int beijing_year = year;

    if (beijing_hour >= 24) {
        beijing_hour -= 24;
        beijing_day++;
        
        // 跨日处理：使用北京时间年份判断闰年
        int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if ((beijing_year % 4 == 0 && beijing_year % 100 != 0) || (beijing_year % 400 == 0)) {
            days_in_month[2] = 29;
        }
        
        if (beijing_day > days_in_month[beijing_month]) {
            beijing_day = 1;
            beijing_month++;
            if (beijing_month > 12) {
                beijing_month = 1;
                beijing_year++;
            }
        }
    }

    // 填充数据
    data->year   = beijing_year;
    data->month  = beijing_month;
    data->day    = beijing_day;
    data->hour   = beijing_hour;
    data->minute = utc_min;
    data->second = utc_sec;
    data->valid  = (status == 'A');

    // 转换经纬度
    double lat_val = strtod(lat_str, NULL);
    int lat_deg = (int)(lat_val / 100);
    double lat_min = lat_val - lat_deg * 100;
    data->latitude = lat_deg + lat_min / 60.0;
    data->lat_dir = lat_dir;

    double lon_val = strtod(lon_str, NULL);
    int lon_deg = (int)(lon_val / 100);
    double lon_min = lon_val - lon_deg * 100;
    data->longitude = lon_deg + lon_min / 60.0;
    data->lon_dir = lon_dir;

    // 计算 UTC 时间戳（简化版）
    int days = 0;
    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days++;
    }
    int dim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) dim[2] = 29;
    for (int m = 1; m < month; m++) {
        days += dim[m];
    }
    days += day - 1;
    data->timestamp = (uint32_t)days * 86400 + utc_hour * 3600 + utc_min * 60 + utc_sec;

    return data->valid;
}

/*=============================================================================
 * @brief   解析 NMEA 数据（从接收缓冲区读取并解析）
 * @note    此函数应在主循环中定期调用。
 *          它会检查是否有新行可用，尝试解析 GGA 或 RMC 语句。
 *          如果解析成功，更新缓冲器；否则保留上一次有效数据。
 *===========================================================================*/
void GPS_ParseNMEA(void)
{
    // 超时检测：如果行缓冲区有数据但超过100ms无新数据，强制提交
    if (gps_line_idx > 0 && (tick_ms - gps_last_rx_time) > 100) {
        __disable_irq();
        gps_line_buf[gps_line_idx] = '\0';
        gps_line_ready = true;
        gps_line_idx = 0;
        __enable_irq();
    }

    if (!gps_line_ready) {
        return;
    }

    // 复制行缓冲区到临时缓冲区
    char line[GPS_LINE_BUF_SIZE];
    __disable_irq();
    memcpy(line, (const char*)gps_line_buf, GPS_LINE_BUF_SIZE);
    gps_line_ready = false;
    __enable_irq();

    // 尝试解析 GGA
    GPS_Data_TypeDef temp_data = {0};
    if (parse_GGA(line, &temp_data)) {
        // 如果 GGA 解析成功，更新缓冲器
        __disable_irq();
        gps_data_buffer = temp_data;
        gps_data_valid = true;
        __enable_irq();
        return;
    }

    // 尝试解析 RMC
    if (parse_RMC(line, &temp_data)) {
        __disable_irq();
        gps_data_buffer = temp_data;
        gps_data_valid = true;
        __enable_irq();
        return;
    }

    // 解析失败，保留上一次有效数据
}

/*=============================================================================
 * @brief   获取最新有效数据
 * @param   data : 输出参数，指向 GPS_Data_TypeDef 结构体
 * @return  true 表示数据有效，false 表示无有效数据
 *===========================================================================*/
bool GPS_GetLatestData(GPS_Data_TypeDef *data)
{
    bool ret;
    __disable_irq();
    if (gps_data_valid) {
        *data = gps_data_buffer;
        ret = true;
    } else {
        ret = false;
    }
    __enable_irq();
    return ret;
}

/*=============================================================================
 * @brief   清除缓冲数据
 *===========================================================================*/
void GPS_ClearData(void)
{
    __disable_irq();
    gps_data_valid = false;
    memset((void*)&gps_data_buffer, 0, sizeof(gps_data_buffer));
    __enable_irq();
}

/*=============================================================================
 * @brief   USART1 中断服务函数
 * @note    RXNE：接收数据写入环形缓冲区
 *          TXE ：从环形缓冲区取出数据发送，发完则关 TXE 中断
 *===========================================================================*/
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART1_IRQHandler(void)
{
    uint8_t byte;

    /* ----- 接收中断 ----- */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        byte = (uint8_t)USART_ReceiveData(USART1);
        __ring_push(&gps_rx_ring, byte, GPS_RX_BUF_SIZE);

        /* 行读取：检测帧头 '$' 和帧尾 '\n' */
        if (byte == '$') {
            // 新帧开始，重置行缓冲区
            gps_line_idx = 0;
        }
        
        if (gps_line_idx < GPS_LINE_BUF_SIZE - 1) {
            gps_line_buf[gps_line_idx++] = byte;
            if (byte == '\n') {
                gps_line_buf[gps_line_idx] = '\0';
                gps_line_ready = true;
                gps_line_idx = 0;
            }
        } else {
            /* 行缓冲区溢出，重置 */
            gps_line_idx = 0;
        }
        
        gps_last_rx_time = tick_ms;  // 记录收到数据的时间
    }

    /* ----- 发送中断 ----- */
    if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET) {
        if (__ring_pop(&gps_tx_ring, &byte, GPS_TX_BUF_SIZE)) {
            USART_SendData(USART1, byte);
        } else {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
            gps_tx_busy = 0;
        }
    }
}
