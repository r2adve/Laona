/*
 * 화면 아래 두 줄 글자 영역
 */
#include <string.h>
#include "text.h"

#define MAX_CHARS   300       /* 한 번에 보여줄 수 있는 글자 수 */
#define MAX_LINES   40
#define LINE_H      16
#define MARGIN      3         /* 좌우 여백 */
#define PAGE_MS     1800      /* 한 쪽 (두 줄) 기본 표시 시간 */
#define CHAR_MS     90        /* 글자 하나당 더 보여주는 시간 */
#define HOLD_MS     5000      /* 마지막 쪽을 보여준 뒤 사라지기까지 */

static const uint8_t *s_font;
static int s_cw, s_ch, s_bpp, s_nranges, s_rec;
static const uint8_t *s_ranges, *s_glyphs;

static uint32_t s_cp[MAX_CHARS];          /* 문장 (유니코드) */
static int s_len;
static int s_line_start[MAX_LINES + 1];   /* 줄마다 시작 위치 */
static int s_nlines;
static int s_page, s_page_t;              /* 지금 보여주는 쪽, 그 쪽을 보여준 시간 */
static bool s_showing, s_thinking;
static int s_think_t;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool text_init(const uint8_t *font, size_t len)
{
    if (len < 12 || memcmp(font, "LAOF", 4) != 0 || font[4] != 1) return false;
    s_font = font;
    s_cw = font[5];
    s_ch = font[6];
    s_bpp = font[7];
    s_nranges = font[8] | font[9] << 8;
    s_ranges = font + 12;
    s_glyphs = s_ranges + s_nranges * 12;
    s_rec = 1 + (s_cw * s_ch * s_bpp + 7) / 8;
    return true;
}

/* 글자 하나의 비트맵 (첫 바이트는 글자 폭). 없는 글자는 '?' */
static const uint8_t *glyph(uint32_t cp)
{
    for (int i = 0; i < s_nranges; i++) {
        const uint8_t *r = s_ranges + i * 12;
        uint32_t first = rd32(r), count = rd32(r + 4);
        if (cp >= first && cp < first + count) {
            return s_glyphs + (rd32(r + 8) + cp - first) * s_rec;
        }
    }
    return cp == '?' ? NULL : glyph('?');
}

static int advance(uint32_t cp)
{
    const uint8_t *g = glyph(cp);
    return g ? g[0] : 0;
}

/* UTF-8 -> 유니코드 (최대 max 글자). 줄바꿈 이외의 제어 문자는 공백으로 */
static int decode_to(const char *s, uint32_t *out, int max)
{
    const uint8_t *p = (const uint8_t *)s;
    int len = 0;
    while (*p && len < max) {
        uint32_t cp;
        int n;
        if (*p < 0x80) { cp = *p; n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { p++; continue; }
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) { n = i; cp = '?'; break; }
            cp = cp << 6 | (p[i] & 0x3F);
        }
        p += n;
        if (cp < 0x20 && cp != '\n') cp = ' ';
        out[len++] = cp;
    }
    return len;
}

static void decode(const char *s)
{
    s_len = decode_to(s, s_cp, MAX_CHARS);
}

/* 화면 폭에 맞춰 줄을 나눔. 가능하면 띄어쓰기에서 나눈다 */
static void wrap(int width)
{
    s_nlines = 0;
    int i = 0;
    while (i < s_len && s_nlines < MAX_LINES) {
        while (i < s_len && s_cp[i] == ' ') i++;          /* 줄 앞 공백 생략 */
        if (i >= s_len) break;
        s_line_start[s_nlines++] = i;
        int w = 0, last_space = -1, j = i;
        for (; j < s_len; j++) {
            if (s_cp[j] == '\n') { j++; break; }
            int a = advance(s_cp[j]);
            if (w + a > width) {
                if (last_space > i) j = last_space;
                break;
            }
            if (s_cp[j] == ' ') last_space = j;
            w += a;
        }
        i = j > i ? j : i + 1;
    }
    s_line_start[s_nlines] = s_len;
}

void text_show(const char *utf8)
{
    decode(utf8);
    wrap(160 - MARGIN * 2);
    s_page = 0;
    s_page_t = 0;
    s_thinking = false;
    s_showing = s_nlines > 0;
}

void text_thinking(bool on)
{
    s_thinking = on;
    s_think_t = 0;
    if (on) s_showing = false;
}

void text_clear(void)
{
    s_showing = s_thinking = false;
}

bool text_active(void)
{
    return s_showing || s_thinking;
}

static int page_chars(int page)
{
    int a = page * 2, b = a + 2 < s_nlines ? a + 2 : s_nlines;
    return s_line_start[b] - s_line_start[a];
}

void text_update(int dt_ms)
{
    s_think_t += dt_ms;
    if (!s_showing) return;
    s_page_t += dt_ms;
    int npages = (s_nlines + 1) / 2;
    int dur = PAGE_MS + page_chars(s_page) * CHAR_MS;
    if (s_page + 1 < npages) {
        if (s_page_t >= dur) { s_page++; s_page_t = 0; }
    } else if (s_page_t >= dur + HOLD_MS) {
        s_showing = false;
    }
}

static uint16_t blend(uint16_t d, uint16_t s, int a)
{
    int dr = d >> 11, dg = (d >> 5) & 0x3F, db = d & 0x1F;
    int sr = s >> 11, sg = (s >> 5) & 0x3F, sb = s & 0x1F;
    return (uint16_t)(((dr + (sr - dr) * a / 255) << 11) | ((dg + (sg - dg) * a / 255) << 5) |
                      (db + (sb - db) * a / 255));
}

static void draw_char(uint16_t *fb, int w, int h, int x, int y, uint32_t cp, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = glyph(cp);
    if (!g) return;
    int adv = g[0], levels = (1 << s_bpp) - 1;
    for (int p = 0; p < s_cw * s_ch; p++) {
        int px = x + p % s_cw, py = y + p / s_cw;
        if (p % s_cw >= adv || px < 0 || px >= w || py < 0 || py >= h) continue;
        int bit = p * s_bpp;
        int v = (g[1 + bit / 8] >> (8 - s_bpp - bit % 8)) & levels;
        if (v) fb[py * w + px] = v == levels ? fg : blend(bg, fg, v * 255 / levels);
    }
}

/* 한 줄을 가운데 정렬해서 그림 */
static void draw_line(uint16_t *fb, int w, int h, int y, const uint32_t *cp, int n, uint16_t fg, uint16_t bg)
{
    while (n > 0 && (cp[n - 1] == ' ' || cp[n - 1] == '\n')) n--;
    int width = 0;
    for (int i = 0; i < n; i++) width += advance(cp[i]);
    int x = (w - width) / 2;
    for (int i = 0; i < n; i++) {
        draw_char(fb, w, h, x, y, cp[i], fg, bg);
        x += advance(cp[i]);
    }
}

void text_draw_line(uint16_t *fb, int w, int h, int y, const char *utf8, uint16_t fg, uint16_t bg)
{
    uint32_t cp[40];
    if (s_font) draw_line(fb, w, h, y, cp, decode_to(utf8, cp, 40), fg, bg);
}

void text_render(uint16_t *fb, int w, int h, uint16_t fg, uint16_t bg)
{
    if (!s_font || !text_active()) return;
    int top = h - TEXT_AREA_H;
    for (int i = top * w; i < w * h; i++) fb[i] = bg;
    int y0 = top + (TEXT_AREA_H - LINE_H * 2) / 2;

    if (s_thinking) {
        /* 점 세 개가 자리를 지킨 채 하나씩 나타남 */
        int n = (s_think_t / 400) % 4, step = advance(0xB7) * 2;
        int x = (w - step * 2 - advance(0xB7)) / 2;
        for (int i = 0; i < n; i++) {
            draw_char(fb, w, h, x + i * step, y0 + LINE_H / 2, 0xB7, fg, bg);
        }
        return;
    }
    for (int k = 0; k < 2; k++) {
        int line = s_page * 2 + k;
        if (line >= s_nlines) break;
        int a = s_line_start[line], b = s_line_start[line + 1];
        draw_line(fb, w, h, y0 + k * LINE_H, s_cp + a, b - a, fg, bg);
    }
}
