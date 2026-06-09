#include "pocsag.h"
#include "Tick.h"

/* ================================================================
 *  POCSAG 协议常量
 * ================================================================ */
#define POCSAG_SYNC_WORD        0x7CD215D8UL
#define POCSAG_IDLE_CODEWORD    0x7A89C197UL
#define POCSAG_CODEWORDS_BATCH  16      /* 一个 batch 含 16 个码字 */
#define POCSAG_BYTES_PER_CW     4
#define POCSAG_BATCH_DATA_BYTES (POCSAG_CODEWORDS_BATCH * POCSAG_BYTES_PER_CW)

/* BCH(31,21) 生成多项式: g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1 */
#define BCH_POLY                0x3D1U   /* 11-bit, 对应上述多项式 */

/* ================================================================
 *  TB/T 3504-2018 BCD 字符映射
 *  标准 9.1.2.6: C=空格, D=负号(-), E=(, F=)
 *  与标准 POCSAG 的映射不同（标准 POCSAG: C=-, D=), E=], F=[）
 * ================================================================ */
static char bcd_to_char(uint8_t nibble)
{
    if (nibble <= 9) {
        return (char)('0' + nibble);
    }
    switch (nibble) {
        case 0xA: return ' ';   /* spare → 空格 */
        case 0xB: return 'U';   /* urgent */
        case 0xC: return ' ';   /* TB/T: C=空格 */
        case 0xD: return '-';   /* TB/T: D=负号 */
        case 0xE: return '(';   /* TB/T: E=( */
        default:  return ')';   /* TB/T: F=) */
    }
}

/* ================================================================
 *  4-bit nibble 位反转
 *  TB/T 3504 消息码字中的每个 BCD 字符是 LSB-first 传输的，
 *  需要反转后才是正确的 BCD 值。
 * ================================================================ */
static uint8_t nibble_reverse(uint8_t n)
{
    n = ((n & 0x5) << 1) | ((n & 0xA) >> 1);
    n = ((n & 0x3) << 2) | ((n & 0xC) >> 2);
    return n;
}

/* ================================================================
 *  原始数据环形缓冲区
 * ================================================================ */
static uint8_t  raw_buf[POCSAG_RAW_BUF_SIZE];
static uint16_t raw_write = 0;
static uint16_t raw_read  = 0;

static inline uint16_t raw_available(void)
{
    return (uint16_t)(raw_write - raw_read);
}

static inline uint8_t raw_peek(uint16_t offset)
{
    return raw_buf[(raw_read + offset) & (POCSAG_RAW_BUF_SIZE - 1)];
}

static inline void raw_consume(uint16_t len)
{
    raw_read += len;
}

/* ================================================================
 *  消息组装状态
 * ================================================================ */
typedef struct {
    uint32_t ric;
    uint8_t  function;
    char     text[POCSAG_MAX_MSG_LEN + 1];
    uint16_t text_len;
    bool     active;
    uint32_t last_time_ms;
} POCSAG_MsgState_t;

static POCSAG_MsgState_t g_msg = {0};
static POCSAG_Callback_t g_callback = 0;

/* ================================================================
 *  工具函数：偶校验
 * ================================================================ */
static inline bool even_parity_ok(uint32_t cw)
{
    uint32_t v = cw;
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1U) == 0;
}

/* ================================================================
 *  工具函数：BCH(31,21) 校验
 *  data31 : 31 位数据 (不含偶校验位)
 *  返回 true 表示余数为 0 (无 BCH 错误)
 * ================================================================ */
static bool bch31_21_check(uint32_t data31)
{
    uint32_t rem = data31 & 0x7FFFFFFFUL;
    const uint32_t poly = BCH_POLY;  /* 0x3D1, 11 bits */

    for (int i = 30; i >= 10; i--) {
        if (rem & (1UL << i)) {
            rem ^= (poly << (i - 10));
        }
    }
    return (rem & 0x3FFU) == 0;
}

/* ================================================================
 *  工具函数：BCH + 偶校验 综合校验, 支持 1-bit 纠错
 *  输入 32-bit 码字 (含偶校验位)
 *  输出纠正后的码字到 *corrected
 *  返回 true 表示校验通过 (或已纠正)
 * ================================================================ */
static bool pocsag_check_codeword(uint32_t cw, uint32_t *corrected)
{
    /* 快速路径: 无错误 */
    if (bch31_21_check(cw >> 1) && even_parity_ok(cw)) {
        *corrected = cw;
        return true;
    }

    /* 1-bit 纠错: 遍历 32 个位置, 翻转后重新校验 */
    for (int i = 0; i < 32; i++) {
        uint32_t test = cw ^ (1UL << i);
        if (bch31_21_check(test >> 1) && even_parity_ok(test)) {
            *corrected = test;
            return true;
        }
    }

    return false;  /* 不可纠正 */
}

/* ================================================================
 *  提交当前消息 (如果有效) 并重置状态
 * ================================================================ */
static void msg_submit(void)
{
    if (g_msg.active && g_msg.text_len > 0 && g_callback) {
        g_msg.text[g_msg.text_len] = '\0';
        g_callback(g_msg.ric, g_msg.function,
                   g_msg.text, g_msg.text_len, 0);
    }
    g_msg.active = false;
    g_msg.text_len = 0;
    g_msg.ric = 0;
    g_msg.function = 0;
}

/* ================================================================
 *  追加 numeric 文本到消息缓冲区
 * ================================================================ */
static void msg_append_numeric(const char *chars, uint8_t len)
{
    if (!g_msg.active) {
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        if (g_msg.text_len >= POCSAG_MAX_MSG_LEN) {
            msg_submit();
            return;
        }
        g_msg.text[g_msg.text_len++] = chars[i];
    }
    g_msg.last_time_ms = tick_ms;
}

/* ================================================================
 *  处理单个码字
 *  cw   : 32-bit 码字
 *  slot : 在 batch 中的位置 (0..15)
 * ================================================================ */
static void process_codeword(uint32_t cw, uint8_t slot)
{
    uint32_t fixed;
    bool ok = pocsag_check_codeword(cw, &fixed);
    if (!ok) {
        return;  /* 不可纠正, 丢弃 */
    }

    /* IDLE 码字 -> 结束当前消息 */
    if (fixed == POCSAG_IDLE_CODEWORD) {
        msg_submit();
        return;
    }

    /* bit31 = 0 -> 地址码字 */
    if ((fixed & 0x80000000UL) == 0) {
        /* 结束之前正在接收的消息 */
        msg_submit();

        /* 提取地址和功能码
         * 地址码字格式:
         *   bit30:13 = 18 位地址高位
         *   bit12:11 = 2 位功能码
         * 实际 21 位 RIC = (addr_high << 3) | frame_number
         * frame_number = slot / 2 (因为每帧 2 个码字, 地址在偶数位)
         */
        uint32_t addr_high = (fixed >> 13) & 0x3FFFFUL;
        uint8_t  func      = (fixed >> 11) & 0x03;
        uint8_t  frame_num = slot >> 1;

        g_msg.ric = (addr_high << 3) | frame_num;
        g_msg.function = func;
        g_msg.active = true;
        g_msg.text_len = 0;
        g_msg.last_time_ms = tick_ms;
        return;
    }

    /* bit31 = 1 -> 消息码字 */
    {
        /* 提取 20 位消息数据 (bit30:11), MSB-first 字符顺序 */
        uint32_t data20 = (fixed >> 11) & 0xFFFFFUL;
        char chars[6];
        uint8_t n;

        for (n = 0; n < 5; n++) {
            /* TB/T: nibbles are sent MSB-char-first but LSB-bit-first within each char */
            uint8_t nibble = (data20 >> (16 - n * 4)) & 0x0F;
#ifdef POCSAG_NIBBLE_REVERSE
            nibble = nibble_reverse(nibble);
#endif
            chars[n] = bcd_to_char(nibble);
        }
        chars[5] = '\0';

        /* 如果没有活跃消息, 忽略孤儿消息码字 */
        if (!g_msg.active) {
            return;
        }

        msg_append_numeric(chars, 5);
    }
}

/* ================================================================
 *  搜索同步字并处理 batch
 * ================================================================ */
static void search_and_process(void)
{
    while (raw_available() >= 4) {
        /* 读取前 4 字节看是否匹配同步字 */
        uint32_t word = ((uint32_t)raw_peek(0) << 24) |
                        ((uint32_t)raw_peek(1) << 16) |
                        ((uint32_t)raw_peek(2) << 8)  |
                        raw_peek(3);

        if (word == POCSAG_SYNC_WORD) {
            /* 找到同步字, 需要后面至少 64 字节数据 */
            if (raw_available() < (4 + POCSAG_BATCH_DATA_BYTES)) {
                return;  /* 数据不足, 等待更多数据 */
            }

            /* 消费同步字 */
            raw_consume(4);

            /* 处理 16 个码字 */
            for (int i = 0; i < POCSAG_CODEWORDS_BATCH; i++) {
                uint32_t cw = ((uint32_t)raw_peek(0) << 24) |
                              ((uint32_t)raw_peek(1) << 16) |
                              ((uint32_t)raw_peek(2) << 8)  |
                              raw_peek(3);
                raw_consume(4);
                process_codeword(cw, (uint8_t)i);
            }
        } else {
            /* 不匹配, 消费 1 字节继续滑动搜索 */
            raw_consume(1);
        }
    }
}

/* ================================================================
 *  对外 API 实现
 * ================================================================ */

void POCSAG_Init(POCSAG_Callback_t callback)
{
    g_callback = callback;
    raw_write = 0;
    raw_read  = 0;
    g_msg.active = false;
    g_msg.text_len = 0;
}

void POCSAG_FeedByte(uint8_t byte)
{
    raw_buf[raw_write & (POCSAG_RAW_BUF_SIZE - 1)] = byte;
    raw_write++;
}

void POCSAG_FeedBytes(const uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        POCSAG_FeedByte(data[i]);
    }
}

void POCSAG_Process(void)
{
    /* 检查消息超时 */
    if (g_msg.active) {
        uint32_t elapsed = tick_ms - g_msg.last_time_ms;
        if (elapsed >= POCSAG_MSG_TIMEOUT_MS) {
            msg_submit();
        }
    }

    /* 处理原始数据缓冲区 */
    search_and_process();
}

void POCSAG_Reset(void)
{
    raw_write = 0;
    raw_read  = 0;
    msg_submit();
}
