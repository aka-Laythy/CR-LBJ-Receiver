#include "lbj.h"
#include "Tick.h"
#include <string.h>

/* ================================================================
 *  LBJ 解析器（依据 TB/T 3504-2018 第 9.1.2 节 +
 *  参照 rtl_sdr_lbj_receiver.py 扩展帧格式）
 * ================================================================ */

/* 机车类型字典 (来自 SDR_LBJ_RECEIVER self.locos, 精选常见类型) */
typedef struct {
    uint16_t    code;
    const char *name;
} LocoEntry_t;

static const LocoEntry_t loco_table[] = {
    {  1, "解放"  }, {  3, "前进"  }, {  5, "建设"  },
    {101, "东风"  }, {104, "东风4" }, {105, "东风4客"}, {106, "东风4C"},
    {107, "东风5" }, {110, "东风7" }, {111, "东风8" }, {131, "东风7C"},
    {138, "东风11"}, {141, "东风4D"}, {142, "东风8B"}, {143, "东风12"},
    {158, "东风11G"},{160, "HXN3"  }, {161, "HXN5"  }, {162, "HXN3B"},
    {163, "HXN5B" }, {167, "FXN3B" }, {170, "FXN5C" }, {171, "FXN3-J"},
    {205, "韶山1" }, {206, "韶山3" }, {207, "韶山4" }, {209, "韶山6" },
    {210, "韶山3B"}, {211, "韶山7" }, {212, "韶山8" }, {214, "韶山7C"},
    {216, "韶山9" }, {217, "韶山7D"}, {224, "韶山7E"}, {225, "韶山4G"},
    {231, "HXD1"  }, {232, "HXD2"  }, {233, "HXD3"  }, {234, "HXD1B"},
    {235, "HXD2B" }, {236, "HXD3B" }, {237, "HXD1C" }, {239, "HXD3C"},
    {240, "HXD1D" }, {242, "HXD3D" }, {243, "FXD1B" }, {245, "FXD1" },
    {246, "FXD3"  }, {247, "FXD1-J"},{248, "FXD3-J"},
    {300, "CRH1"  }, {301, "CRH2"  }, {302, "CRH3"  }, {304, "CRH5"  },
    {305, "CRH380A"},{306, "CRH380B"},{308, "CRH380D"},{309, "CRH6A" },
    {310, "CR400AF"},{311, "CR400BF"},{312, "CR300AF"},{313, "CR300BF"},
    {329, "CJ1"   }, {330, "CJ2"   }, {331, "CJ3"   }, {334, "CJ6"   },
    {  0, NULL     }
};

static const char * loco_lookup(uint16_t code) {
    for (uint8_t i = 0; loco_table[i].name != NULL; i++) {
        if (loco_table[i].code == code) return loco_table[i].name;
    }
    return NULL;
}

/* ================================================================
 *  BCD 字符 → hex nibble 反向映射
 *  与 pocsag.c bcd_to_char 互逆:
 *    '0'-'9'→0-9, '*'(0xA)→0xA, 'U'(0xB)→0xB,
 *    ' '(0xC)→0xC, '-'(0xD)→0xD, ')'(0xE)→0xE, '('(0xF)→0xF
 * ================================================================ */
static uint8_t bcd_to_nibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    switch (c) {
        case '*': return 0x0A;
        case 'U': return 0x0B;
        case ' ': return 0x0C;
        case '-': return 0x0D;
        case ')': return 0x0E;
        case '(': return 0x0F;
        default:  return 0x00;
    }
}

/* ================================================================
 *  Session 管理 (按车次号关联基本帧和扩展帧)
 * ================================================================ */

static uint8_t hex_pair_to_byte(const char *hex, uint8_t idx) {
    return (uint8_t)((hex[idx] << 4) | hex[idx + 1]);
}

typedef struct {
    char     train_id[8];
    char     prefix[4];
    uint8_t  direction;     /* 1=下行, 3=上行 */
    char     speed_str[4];
    char     position[8];
    uint16_t loco_type_code;
    char     loco_number[8];
    uint8_t  route_gbk[32];
    uint8_t  route_gbk_len;
    bool     is_extended;
    uint32_t last_ms;
    bool     used;
} Session_t;

static Session_t g_sessions[LBJ_MAX_SESSIONS];
static char        g_last_train[8] = "";
static LBJ_Message_t g_latest_msg;
static LBJ_Callback_t g_lbj_cb = 0;
static bool g_latest_valid = false;

static Session_t * session_find(const char *train_id) {
    for (uint8_t i = 0; i < LBJ_MAX_SESSIONS; i++) {
        if (g_sessions[i].used && strcmp(g_sessions[i].train_id, train_id) == 0) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

static Session_t * session_alloc(const char *train_id) {
    /* 找已有或空闲 slot */
    uint8_t oldest_i = 0;
    uint32_t oldest_ms = 0xFFFFFFFF;
    for (uint8_t i = 0; i < LBJ_MAX_SESSIONS; i++) {
        if (!g_sessions[i].used) { oldest_i = i; break; }
        if (g_sessions[i].last_ms < oldest_ms) {
            oldest_ms = g_sessions[i].last_ms;
            oldest_i = i;
        }
    }
    Session_t *s = &g_sessions[oldest_i];
    memset(s, 0, sizeof(Session_t));
    strncpy(s->train_id, train_id, sizeof(s->train_id) - 1);
    s->used = true;
    return s;
}

static void sessions_expire(void) {
    uint32_t now = tick_ms;
    for (uint8_t i = 0; i < LBJ_MAX_SESSIONS; i++) {
        if (g_sessions[i].used && (now - g_sessions[i].last_ms) > LBJ_SESSION_TTL_MS) {
            g_sessions[i].used = false;
        }
    }
}

static void session_merge_output(Session_t *s) {
    LBJ_Message_t *m = &g_latest_msg;
    memset(m, 0, sizeof(*m));
    m->ric = LBJ_RIC_APPROACH;

    /* 车次号 = prefix + train_id */
    if (s->prefix[0]) {
        uint8_t pl = (uint8_t)strlen(s->prefix);
        memcpy(m->train_id, s->prefix, pl);
        memcpy(m->train_id + pl, s->train_id, sizeof(m->train_id) - pl - 1);
    } else {
        strncpy(m->train_id, s->train_id, sizeof(m->train_id) - 1);
    }
    m->has_train_id = true;
    m->train_odd_even = (s->direction == 3);

    /* 速度 */
    if (s->speed_str[0]) {
        m->has_speed = true;
        m->speed = 0;
        for (uint8_t i = 0; s->speed_str[i]; i++) {
            if (s->speed_str[i] >= '0' && s->speed_str[i] <= '9')
                m->speed = m->speed * 10 + (s->speed_str[i] - '0');
        }
    }

    /* 公里标 */
    if (s->position[0]) {
        m->has_km = true;
        strncpy(m->km_post, s->position, sizeof(m->km_post) - 1);
    }

    /* 扩展帧信息 */
    m->is_extended = s->is_extended;
    if (s->is_extended) {
        strncpy(m->prefix, s->prefix, sizeof(m->prefix) - 1);
        m->loco_type_code = s->loco_type_code;
        const char *ln = loco_lookup(s->loco_type_code);
        if (ln) {
            strncpy(m->loco_model, ln, sizeof(m->loco_model) - 1);
        }
        strncpy(m->loco_number, s->loco_number, sizeof(m->loco_number) - 1);
        memcpy(m->route_gbk, s->route_gbk, s->route_gbk_len);
        m->route_gbk_len = s->route_gbk_len;
    }

    g_latest_valid = true;
    if (g_lbj_cb) g_lbj_cb(m);
}

/* ================================================================
 *  解析扩展帧 (RIC 1234001 / 1234002)
 *  参照 rtl_sdr_lbj_receiver.py decode_lbj()
 * ================================================================ */
static void parse_extended(const char *text, uint8_t len, Session_t *s) {
    /* 取末尾 50 字符 */
    uint8_t buf_start = (len > 50) ? (uint8_t)(len - 50) : 0;
    uint8_t buf_len   = (len > 50) ? 50 : len;
    const char *buf = text + buf_start;

    /* 转为 hex nibble 字符串 */
    char ih[52];
    uint8_t ih_len = 0;
    for (uint8_t i = 0; i < buf_len && ih_len < 50; i++) {
        ih[ih_len++] = (char)bcd_to_nibble(buf[i]);
    }
    ih[ih_len] = '\0';

    /* 机车字母前缀: hex[0:2] + hex[2:4] → 2 字节 ASCII */
    if (ih_len >= 4) {
        uint8_t c1 = hex_pair_to_byte(ih, 0);
        uint8_t c2 = hex_pair_to_byte(ih, 2);
        uint8_t p = 0;
        if ((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'Z'))
            s->prefix[p++] = (char)c1;
        if ((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'Z'))
            s->prefix[p++] = (char)c2;
        s->prefix[p] = '\0';
    }

    /* 机型代码 + 编号: buf[4:12] (8 BCD chars) */
    if (buf_len >= 12) {
        /* 前 3 digit = 机型代码 */
        char code_str[4] = {0};
        uint8_t ci = 0;
        for (uint8_t i = 4; i < 7 && i < buf_len; i++) {
            if (buf[i] >= '0' && buf[i] <= '9') code_str[ci++] = buf[i];
        }
        if (ci > 0) s->loco_type_code = (uint16_t)(code_str[0] - '0');
        if (ci > 1) s->loco_type_code = s->loco_type_code * 10 + (uint16_t)(code_str[1] - '0');
        if (ci > 2) s->loco_type_code = s->loco_type_code * 10 + (uint16_t)(code_str[2] - '0');

        /* 后续 = 机车编号 */
        uint8_t ni = 0;
        for (uint8_t i = 7; i < 12 && i < buf_len && ni < 7; i++) {
            if (buf[i] >= '0' && buf[i] <= '9') s->loco_number[ni++] = buf[i];
        }
        s->loco_number[ni] = '\0';
    }

    /* 线路名: hex[14:30] → GBK 字节 */
    if (ih_len >= 30) {
        uint8_t rlen = 0;
        for (uint8_t i = 14; i + 1 < ih_len && rlen < 31; i += 2) {
            uint8_t b = hex_pair_to_byte(ih, i);
            if (b == 0x00 || b == 0xFF) continue;
            s->route_gbk[rlen++] = b;
        }
        s->route_gbk[rlen] = '\0';
        s->route_gbk_len = rlen;
    }

    s->is_extended = true;
    s->last_ms = tick_ms;
}

/* ================================================================
 *  主解析函数
 * ================================================================ */
void LBJ_ParsePOCSAG(uint32_t ric, uint8_t function,
                      const char *text, uint8_t len,
                      int16_t rssi)
{
    if (len > 250) len = 250;
    sessions_expire();

    /* 时钟校准 RIC → 忽略 */
    if (ric == LBJ_RIC_CLOCK) return;

    bool is_basic    = (ric == LBJ_RIC_APPROACH && len >= 13);
    bool is_merged   = (ric == LBJ_RIC_APPROACH && len >= 65);
    bool is_extended = (ric == LBJ_RIC_EXTENDED && len < 65);

    /* 合并帧也按扩展帧处理后半部分 */
    bool has_ext_in_merge = (ric == LBJ_RIC_APPROACH && len >= 65);

    /* === 基本帧解析 === */
    if (is_basic || is_merged) {
        char tid[8] = "";
        uint8_t tj = 0;
        /* 车次号 0..4, 只取数字和字母 */
        for (uint8_t i = 0; i < 5 && i < len && tj < 7; i++) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                tid[tj++] = c;
        }
        tid[tj] = '\0';
        if (tj == 0) return;

        /* 检查是否全 '-' */
        bool valid = false;
        for (uint8_t i = 0; i < 5 && i < len; i++) {
            if (text[i] != '-') { valid = true; break; }
        }
        if (!valid) return;

        Session_t *s = session_find(tid);
        if (!s) s = session_alloc(tid);
        s->direction = function;
        s->last_ms = tick_ms;

        /* 速度 5..7 */
        {
            char sp[4] = {0};
            uint8_t sj = 0;
            for (uint8_t i = 5; i < 8 && i < len && sj < 3; i++) {
                if (text[i] >= '0' && text[i] <= '9') sp[sj++] = text[i];
            }
            sp[sj] = '\0';
            if (sj > 0) strncpy(s->speed_str, sp, sizeof(s->speed_str) - 1);
        }

        /* 公里标 8..12 */
        {
            char km[8] = {0};
            uint8_t kj = 0;
            for (uint8_t i = 8; i < 13 && i < len && kj < 7; i++) {
                if (text[i] >= '0' && text[i] <= '9') km[kj++] = text[i];
            }
            km[kj] = '\0';
            if (kj > 0) strncpy(s->position, km, sizeof(s->position) - 1);
        }

        strncpy(g_last_train, tid, sizeof(g_last_train) - 1);

        /* 合并帧: 同时解析扩展部分 */
        if (has_ext_in_merge) {
            parse_extended(text, len, s);
        }

        session_merge_output(s);
    }

    /* === 独立扩展帧解析 === */
    if (is_extended) {
        Session_t *s = session_find(g_last_train);
        if (s && (tick_ms - s->last_ms) < 2000) {
            parse_extended(text, len, s);
            session_merge_output(s);
        }
    }
}

/* ================================================================
 *  获取最近一次完整解析的 LBJ 消息
 * ================================================================ */
const LBJ_Message_t * LBJ_GetLatest(void)
{
    if (g_latest_valid) return &g_latest_msg;
    return NULL;
}

/* ================================================================
 *  初始化
 * ================================================================ */
void LBJ_Init(LBJ_Callback_t cb)
{
    g_lbj_cb = cb;
    memset(g_sessions, 0, sizeof(g_sessions));
    memset(&g_latest_msg, 0, sizeof(g_latest_msg));
    memset(g_last_train, 0, sizeof(g_last_train));
    g_latest_valid = false;
}
