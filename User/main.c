#include "debug.h"
#include "main.h"
#include "Tick.h"
#include "schedule.h"
#include "Key.h"
#include "BLE.h"
#include "GPS.h"
#include "OLED/Menu.h"
#include "SPI_Bus/spi_bus.h"
#include "SX1276/sx1276.h"
#include "SPI_Flash/spi_flash.h"
#include "bit_capture.h"
#include "POCSAG/pocsag.h"
#include "LBJ/lbj.h"
#include "str_util.h"
#include <string.h>

/* 串口调试输出宏 — 使用 BLE/USART2（PD2 TX, 9600） */
#define DBG(msg)    BLE_SendString((const uint8_t *)(msg))
#define DBGx(msg)   do { DBG(msg); Tick_DelayMs(10); } while(0)

/* ================================================================
 *  全局运行统计
 * ================================================================ */
static volatile uint32_t g_lbj_rx_count = 0;
static volatile int16_t  g_last_rssi = -128;
static volatile uint32_t g_heartbeat_ms = 0;
static volatile uint32_t g_last_irq_count = 0;

/* ================================================================
 *  POCSAG 解码回调
 *  收到完整 POCSAG 消息后触发
 * ================================================================ */
static void on_pocsag_message(uint32_t ric, uint8_t function,
                               const char *text, uint8_t len,
                               int16_t rssi)
{
    (void)rssi;

    /* 送入 LBJ 解析层 */
    LBJ_ParsePOCSAG(ric, function, text, len, g_last_rssi);
}

/* ================================================================
 *  LBJ 解析回调
 *  收到完整 LBJ 消息后触发，通过蓝牙输出
 * ================================================================ */
static void on_lbj_message(const LBJ_Message_t *msg)
{
    g_lbj_rx_count++;

    char out[384];
    int n = 0;

    memcpy(out + n, "[LBJ] RIC=", 10); n += 10;
    n += utox32(out + n, msg->ric, 6);
    memcpy(out + n, " Fn=", 4); n += 4;
    n += utoa16(out + n, msg->function);
    memcpy(out + n, " RSSI=", 6); n += 6;
    n += itoa16(out + n, msg->rssi_dbm);
    memcpy(out + n, "\r\n", 2); n += 2;

    if (msg->has_train_id) {
        memcpy(out + n, "  Train=", 8); n += 8;
        uint8_t tlen = 0;
        while (msg->train_id[tlen]) tlen++;
        memcpy(out + n, msg->train_id, tlen); n += tlen;
        memcpy(out + n, " (", 2); n += 2;
        memcpy(out + n, msg->train_odd_even ? "even" : "odd", msg->train_odd_even ? 4 : 3);
        n += msg->train_odd_even ? 4 : 3;
        memcpy(out + n, ")\r\n", 3); n += 3;
    }
    if (msg->has_speed) {
        memcpy(out + n, "  Speed=", 8); n += 8;
        n += itoa16(out + n, msg->speed);
        memcpy(out + n, " km/h\r\n", 7); n += 7;
    }
    if (msg->has_km) {
        memcpy(out + n, "  KM=", 5); n += 5;
        if (msg->km_negative) { out[n++] = '-'; }
        uint8_t klen = 0;
        while (msg->km_post[klen]) klen++;
        memcpy(out + n, msg->km_post, klen); n += klen;
        memcpy(out + n, "\r\n", 2); n += 2;
    }

    memcpy(out + n, "  Raw=", 6); n += 6;
    {
        uint8_t rlen = msg->raw_len;
        if (rlen > 255) rlen = 255;
        if (n + rlen + 4 >= (int)sizeof(out)) rlen = (uint8_t)(sizeof(out) - n - 4);
        memcpy(out + n, msg->raw_text, rlen); n += rlen;
    }
    memcpy(out + n, "\r\n", 2); n += 2;

    BLE_SendData((const uint8_t *)out, (uint16_t)n);
}

/* ================================================================
 *  调度任务 0: GPS NMEA 解析
 * ================================================================ */
void schedule_task0(void)
{
    GPS_ParseNMEA();
}

/* ================================================================
 *  调度任务 1: SX1276 射频接收处理
 *  ISR (bit_capture_isr) 在 DCLK 上升沿采集 bit 并组装 byte。
 *  本任务从 bit 缓冲区取字节送入 POCSAG 解码器。
 * ================================================================ */
void schedule_task1(void)
{
    int16_t rssi;
    uint32_t irqs;
    char hb[64];
    int n;

    /* 读取 RSSI */
    rssi = SX1276_ReadRSSI();
    g_last_rssi = rssi;

    /* 从 bit_capture 取出组装好的字节, 送入 POCSAG */
    while (bit_capture_available() > 0) {
        POCSAG_FeedByte(bit_capture_get_byte());
    }

    POCSAG_Process();

    /* RSSI + EXTI 心跳: 每 5 秒输出一次 */
    if ((uint32_t)(tick_ms - g_heartbeat_ms) >= 5000) {
        g_heartbeat_ms = tick_ms;
        irqs = bit_capture_irq_count();
        n = 0;
        memcpy(hb + n, "[HB] RSSI=", 10); n += 10;
        n += itoa16(hb + n, rssi);
        memcpy(hb + n, " EXTI=", 6); n += 6;
        n += utoa16(hb + n, (uint16_t)((irqs - g_last_irq_count) / 5));
        memcpy(hb + n, "Hz Ovf=", 7); n += 7;
        n += utoa16(hb + n, bit_capture_overflow());
        memcpy(hb + n, " Msgs=", 6); n += 6;
        n += utoa16(hb + n, (uint16_t)g_lbj_rx_count);
        memcpy(hb + n, "\r\n", 2); n += 2;
        BLE_SendData((const uint8_t *)hb, (uint16_t)n);
        g_last_irq_count = irqs;
    }
}

/* ================================================================
 *  系统基础初始化
 * ================================================================ */
int8_t essentials(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    __enable_irq();
    Tick_Init();            // SysTick 1ms 全局嘀嗒时钟
    return 1;
}

/* ================================================================
 *  主函数
 * ================================================================ */
int main(void)
{
    while(!essentials());

    /* ---- 初始化 BLE/USART2 PD2 TX 9600 串口输出 ---- */
    BLE_Init();
    DBG("[BOOT] BLE init OK\r\n");

    DBG("[BOOT] GPS init...\r\n");
    GPS_Init();
    DBG("[BOOT] GPS init OK\r\n");

    DBG("[BOOT] SPI init...\r\n");
    SPI_Bus_Init();
    DBG("[BOOT] SPI init OK\r\n");

    if (SPI_Flash_Init())
        DBG("[BOOT] Flash init OK\r\n");
    else
        DBG("[BOOT] Flash not found\r\n");

    DBG("[BOOT] KEY init...\r\n");
    Key_Init();
    DBG("[BOOT] KEY init OK\r\n");

    DBG("[BOOT] OLED/Menu init...\r\n");
    Menu_Init();
    DBG("[BOOT] OLED/Menu init OK\r\n");

    DBG("[BOOT] SX1276 init...\r\n");
    if (SX1276_Init() != SX1276_OK) {
        DBG("[ERR] SX1276 not found\r\n");
    } else {
        DBG("[OK] SX1276 init done\r\n");
        SX1276_SelfTest(BLE_SendData);
    }

    DBG("[BOOT] bit_capture init...\r\n");
    bit_capture_init();

    DBG("[BOOT] POCSAG init...\r\n");
    POCSAG_Init(on_pocsag_message);

    DBG("[BOOT] LBJ init...\r\n");
    LBJ_Init(on_lbj_message);

    /* 调度器初始化
       task0 周期 10ms (GPS),
       task1 周期 20ms (SX1276),
       Menu_Task 周期 10ms (OLED Menu) */
    DBG("[BOOT] Scheduler start...\r\n");
    schedule_init(10);
    schedule_register(20, schedule_task1);
    schedule_register(10, Menu_Task);
    DBG("[BOOT] Boot complete\r\n");

    while (1) {
        schedule_poll();
    }
}
