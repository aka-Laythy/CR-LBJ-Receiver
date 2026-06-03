/* gps.c */
#include "gps.h"

extern volatile uint32_t tick;

// 串口ringbuffer收
static uint8_t rx_ringbuf[GPS_RX_BUF_SIZE];
static volatile uint16_t rx_write_idx = 0;
static uint16_t rx_read_idx = 0;

// 完整一帧NEMA数据
char gps_latest_frame[GPS_FRAME_BUF_SIZE];
uint16_t gps_latest_frame_len = 0;
static volatile bool frame_ready = false;

// 帧组装
static char asm_buf[GPS_FRAME_BUF_SIZE];
static uint16_t asm_len = 0;
static uint32_t last_rx_time = 0;  // 上次收到字节的时间

// 对外暴露的经纬度、日期时间结构体（其他地方extern使用）
GPS_Coord_TypeDef gps_coord = {0, 'N', 0, 'E', false};
GPS_Time_TypeDef gps_time =   {0, 0, 0, 0, 0, 0, 0, false};

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
        uint8_t data = USART_ReceiveData(USART2);
        
        uint16_t next = (rx_write_idx + 1) % GPS_RX_BUF_SIZE;
        if (next != rx_read_idx) {
            rx_ringbuf[rx_write_idx] = data;
            rx_write_idx = next;
        }
        
        last_rx_time = tick;  // 记录收到数据的时间
    }
}

void GPS_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    USART_InitTypeDef USART_InitStruct = {0};
    NVIC_InitTypeDef NVIC_InitStruct = {0};
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    USART_InitStruct.USART_BaudRate = 115200;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART2, &USART_InitStruct);
    
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    
    NVIC_InitStruct.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    USART_Cmd(USART2, ENABLE);
    
    rx_write_idx = 0;
    rx_read_idx = 0;
    asm_len = 0;
    last_rx_time = 0;
    frame_ready = false;

    printf("[GPS]>>> Init SUCCESS!\r\n");
}

void GPS_CheckNewFrame(void)
{
    // 检查是否满足提交条件：有数据且100ms无新数据
    if (asm_len > 0 && (tick - last_rx_time) > 100) {
        // 提交帧
        if (asm_len < GPS_FRAME_BUF_SIZE) {
            memcpy(gps_latest_frame, asm_buf, asm_len);
            gps_latest_frame[asm_len] = '\0';
            gps_latest_frame_len = asm_len;
            frame_ready = true;
        }
        // 重置组装缓冲区
        asm_len = 0;
    }
    
    // 从ringbuffer转移数据到组装缓冲区
    while (rx_read_idx != rx_write_idx) {
        uint8_t ch = rx_ringbuf[rx_read_idx];
        rx_read_idx = (rx_read_idx + 1) % GPS_RX_BUF_SIZE;
        
        if (asm_len < GPS_FRAME_BUF_SIZE - 1) {
            asm_buf[asm_len++] = ch;
        }
    }
}

bool GPS_GetLatestFrame(u8 *buf, uint16_t *len)
{
    if (!buf || !len) return false;
    if (!frame_ready) return false;
    
    memcpy(buf, gps_latest_frame, gps_latest_frame_len);
    buf[gps_latest_frame_len] = '\0';
    *len = gps_latest_frame_len;
    frame_ready = false;
    
    return true;
}

/* ========== 解析GNGGA 获取经纬度 ========== */
bool GPS_GetCoord(void)
{
    __disable_irq();
    char frame_copy[GPS_FRAME_BUF_SIZE];
    memcpy(frame_copy, gps_latest_frame, gps_latest_frame_len);
    frame_copy[gps_latest_frame_len] = '\0';
    __enable_irq();
    
    // 查找$GNGGA或$GPGGA
    char *gga = strstr(frame_copy, "$GNGGA");
    if (!gga) gga = strstr(frame_copy, "$GPGGA");
    if (!gga) {
        gps_coord.valid = false;
        return false;
    }
    
    // 找到行尾
    char *line_end = strchr(gga, '\n');
    if (!line_end) line_end = strchr(gga, '\r');
    if (!line_end) line_end = gga + strlen(gga);
    
    // 临时截断，只处理这一行
    char save_char = *line_end;
    *line_end = '\0';
    
    // 手动解析，不用strtok
    char *p = gga;
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
            
            // 字段内容起始
            char *field_start = p;
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
    *line_end = save_char;
    
    // 验证
    if (lat_str[0] == 0 || lon_str[0] == 0 || 
        (lat_dir != 'N' && lat_dir != 'S') || 
        (lon_dir != 'E' && lon_dir != 'W')) {
        gps_coord.valid = false;
        return false;
    }
    
    // 转换纬度：ddmm.mmmm → 度
    // 替换 sscanf 部分
    double lat_val = strtod(lat_str, NULL);
    int lat_deg = (int)(lat_val / 100);
    double lat_min = lat_val - lat_deg * 100;
    gps_coord.latitude = lat_deg + lat_min / 60.0;
    gps_coord.lat_dir = lat_dir;
    
    // 转换经度：dddmm.mmmm → 度
    double lon_val = strtod(lon_str, NULL);
    sscanf(lon_str, "%lf", &lon_val);
    int lon_deg = (int)(lon_val / 100);
    double lon_min = lon_val - lon_deg * 100;
    gps_coord.longitude = lon_deg + lon_min / 60.0;
    gps_coord.lon_dir = lon_dir;
    
    // 定位质量0=无效, 1=GPS, 2=DGPS
    gps_coord.valid = (fix_quality >= '1');
    
    return gps_coord.valid;
}

/* ========== 解析GNZDA 获取时间 ========== */
bool GPS_GetTime(void)
{
    __disable_irq();
    char frame_copy[GPS_FRAME_BUF_SIZE];
    memcpy(frame_copy, gps_latest_frame, gps_latest_frame_len);
    frame_copy[gps_latest_frame_len] = '\0';
    __enable_irq();
    
    // 查找$GNZDA或$GPZDA
    char *zda = strstr(frame_copy, "$GNZDA");
    if (!zda) zda = strstr(frame_copy, "$GPZDA");
    if (!zda) {
        gps_time.valid = false;
        return false;
    }
    
    // 解析：$GNZDA,hhmmss.ss,dd,mm,yyyy,xx,yy*hh
    // 字段：时间,日,月,年,时区小时,时区分钟
    char *p = zda;
    int field = 0;
    char time_str[16] = {0};
    int day = 0, month = 0, year = 0;
    
    while (*p && field < 7) {
        if (*p == ',') {
            field++;
            p++;
            char *start = p;
            while (*p && *p != ',' && *p != '*') p++;
            int len = p - start;
            
            switch (field) {
                case 1: // UTC时间 hhmmss.ss
                    if (len >= 6 && len < 16) {
                        memcpy(time_str, start, len);
                        time_str[len] = '\0';
                    }
                    break;
                case 2: // 日
                    if (len > 0) day = atoi(start);
                    break;
                case 3: // 月
                    if (len > 0) month = atoi(start);
                    break;
                case 4: // 年
                    if (len > 0) year = atoi(start);
                    break;
            }
        } else {
            p++;
        }
    }
    
    // 验证
    if (time_str[0] == 0 || day == 0 || month == 0 || year < 2000) {
        gps_time.valid = false;
        return false;
    }
    
    // 解析UTC时间
    int utc_hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int utc_min = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    int utc_sec = (time_str[4] - '0') * 10 + (time_str[5] - '0');
    
    // 转换为北京时间（UTC+8）
    int beijing_hour = utc_hour + 8;
    int beijing_day = day;
    int beijing_month = month;
    int beijing_year = year;
    
    // 处理跨日
    if (beijing_hour >= 24) {
        beijing_hour -= 24;
        beijing_day++;
        
        // 简单处理月份天数（不考虑闰年2月29日特殊情况）
        int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            days_in_month[2] = 29; // 闰年
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
    
    gps_time.year = beijing_year;
    gps_time.month = beijing_month;
    gps_time.day = beijing_day;
    gps_time.hour = beijing_hour;
    gps_time.minute = utc_min;
    gps_time.second = utc_sec;
    gps_time.valid = true;
    
    // 计算UTC时间戳（简化版，不考虑闰秒）
    // 计算从1970年1月1日到当前日期的天数
    int days = 0;
    
    // 整年天数
    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days++;
    }
    
    // 整月天数
    int dim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) dim[2] = 29;
    for (int m = 1; m < month; m++) {
        days += dim[m];
    }
    
    // 当月天数
    days += day - 1;
    
    // 总秒数 = 天数*86400 + 小时*3600 + 分钟*60 + 秒
    gps_time.timestamp = (uint32_t)days * 86400 + utc_hour * 3600 + utc_min * 60 + utc_sec;
    
    return true;
}
