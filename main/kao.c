/*
 * 이모티콘(카오모지) 얼굴
 *
 *   흰 바탕에 선으로 그린 눈과 입. 선 가장자리는 부드럽게 섞어(안티에일리어싱) 그린다.
 *   움직임: 숨쉬듯 위아래로 흔들림, 깜빡임, 두리번거림, 표정 바꿀 때 눈을 감았다 뜸,
 *          표정별 애니메이션 (소용돌이 회전, 눈물, 오물거리는 입, zZ, 떨림 등)
 *
 *   좌표는 "설계 단위" 로 정하고 SC 배로 키워 화면 픽셀로 바꾼다.
 */
#include <math.h>
#include <string.h>
#include "kao.h"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* 바탕과 선 색. 반전 모드에서는 둘을 바꾼다 */
#define C_PAPER   RGB(255, 255, 255)
#define C_INK     RGB(40, 36, 48)
#define C_DARK    RGB(0, 0, 0)
#define C_LIGHT   RGB(255, 255, 255)
#define C_BLUSH   RGB(255, 130, 160)
#define C_TEAR    RGB(80, 170, 255)
#define C_MOUTH   RGB(200, 50, 70)
#define C_TONGUE  RGB(255, 120, 140)

#define SC        2.2f      /* 설계 단위 -> 픽셀 */
#define LW        1.0f      /* 기본 선 반굵기 (설계 단위) */
#define EYE_X     23.0f     /* 눈 중심 (얼굴 중심 기준) */
#define MOUTH_Y   5.5f
#define FACE_Y    62.0f     /* 얼굴 중심 높이 (픽셀) */
#define BLINK_MS  220
#define NONE      -1e9f     /* ellipse() 의 clip_top 미사용 */

enum { E_DOT, E_BIGDOT, E_CHEVRON, E_RING, E_SPIRAL, E_T, E_LINE, E_BAR, E_ANGRY, E_TARGET, E_SIDE };
enum { M_OMEGA, M_SMILE, M_O, M_OPEN, M_TONGUE, M_WAVY, M_FLAT, M_ZIGZAG, M_YAWN };
enum { X_BLUSH = 1, X_TEARS = 2, X_ZZZ = 4, X_SHAKE = 8 };

static const struct {
    const char *name;
    uint8_t eye_l, eye_r, mouth, extra;
} s_expr[] = {
    { "normal",    E_DOT,     E_DOT,     M_OMEGA,  X_BLUSH },   /* • ω •   */
    { "happy",     E_CHEVRON, E_CHEVRON, M_OMEGA,  X_BLUSH },   /* > ω <   */
    { "surprised", E_RING,    E_RING,    M_O,      0 },         /* O o O   */
    { "dizzy",     E_SPIRAL,  E_SPIRAL,  M_WAVY,   0 },         /* @ ~ @   */
    { "cry",       E_T,       E_T,       M_OMEGA,  X_TEARS },   /* T ω T   */
    { "wink",      E_CHEVRON, E_BIGDOT,  M_SMILE,  X_BLUSH },   /* > ‿ ●   */
    { "sleepy",    E_LINE,    E_LINE,    M_SMILE,  X_ZZZ },     /* - ‿ - z */
    { "angry",     E_ANGRY,   E_ANGRY,   M_ZIGZAG, X_SHAKE },   /* ˋ• ʌʌ •ˊ */
    { "stare",     E_TARGET,  E_TARGET,  M_FLAT,   0 },         /* ◎ _ ◎   */
    { "tongue",    E_DOT,     E_DOT,     M_TONGUE, X_BLUSH },   /* • ᴗ̈ •  */
    { "meh",       E_BAR,     E_BAR,     M_FLAT,   0 },         /* ▬ _ ▬   */
    { "glance",    E_SIDE,    E_SIDE,    M_OPEN,   0 },         /* ◑ ▽ ◑   */
    /* 아래는 자동 표정 변경에서 고르지 않음 */
    { "yawn",      E_CHEVRON, E_CHEVRON, M_YAWN,   0 },         /* > O <  하품 */
};
#define NUM_EXPR ((int)(sizeof(s_expr) / sizeof(s_expr[0])))
#define NUM_AUTO (NUM_EXPR - 1)
#define YAWN_MS  1500     /* 하품 입이 벌어졌다 닫히는 시간 */

static uint16_t *s_fb;
static uint8_t *s_cov;                 /* 선 농도 (0~255). 같은 색 선이 겹쳐도 진해지지 않게 최대값만 남김 */
static int s_bx0, s_by0, s_bx1, s_by1; /* s_cov 에 그린 영역 */
static uint32_t s_rng;

static uint32_t s_t;                   /* ms */
static int s_cur, s_next = -1;         /* 현재 표정, 눈 감은 뒤 바꿀 표정 */
static uint32_t s_expr_start;          /* 지금 표정이 시작된 시각 */
static int s_blink_t = -1;             /* 깜빡임 진행 시간, -1 이면 안 깜빡임 */
static bool s_auto = true;
static bool s_invert;                  /* 검은 바탕 + 흰 선 */
static uint32_t s_next_blink, s_next_look, s_next_auto, s_next_chew, s_chew_until;
static float s_lx, s_ly, s_ltx, s_lty; /* 바라보는 방향 (-1 ~ 1) */
static float s_ox, s_oy, s_sy;         /* 이번 프레임 얼굴 중심 (픽셀), 눈 세로 비율 */
static float s_offset;                 /* 얼굴 위아래 이동 (글자 영역을 피할 때) */

static int rnd(int lo, int hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return lo + (int)((s_rng >> 8) % (uint32_t)(hi - lo + 1));
}

/* ---------- 래스터 ---------- */

static uint32_t isqrt(uint32_t n)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else { r >>= 1; }
        b >>= 2;
    }
    return r;
}

static uint64_t isqrt64(uint64_t n)
{
    uint64_t r = 0, b = 1ull << 62;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else { r >>= 1; }
        b >>= 2;
    }
    return r;
}

/* 영역을 화면 안으로 자르고 그린 영역에 포함 */
static bool clip_box(int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > KAO_W - 1) *x1 = KAO_W - 1;
    if (*y1 > KAO_H - 1) *y1 = KAO_H - 1;
    if (*x0 > *x1 || *y0 > *y1) return false;
    if (*x0 < s_bx0) s_bx0 = *x0;
    if (*y0 < s_by0) s_by0 = *y0;
    if (*x1 > s_bx1) s_bx1 = *x1;
    if (*y1 > s_by1) s_by1 = *y1;
    return true;
}

static inline void cov_max(int x, int y, int a)
{
    if (a <= 0) return;
    if (a > 255) a = 255;
    uint8_t *p = &s_cov[y * KAO_W + x];
    if (a > *p) *p = (uint8_t)a;
}

/* 굵기 반지름 r 인 선분 (픽셀 좌표). 정수 연산 (1/16 픽셀 단위) */
static void seg(float ax, float ay, float bx, float by, float r)
{
    int x0 = (int)floorf(fminf(ax, bx) - r - 1), x1 = (int)ceilf(fmaxf(ax, bx) + r + 1);
    int y0 = (int)floorf(fminf(ay, by) - r - 1), y1 = (int)ceilf(fmaxf(ay, by) + r + 1);
    if (!clip_box(&x0, &y0, &x1, &y1)) return;

    int R = (int)(r * 16), A_x = (int)(ax * 16), A_y = (int)(ay * 16);
    int ex = (int)((bx - ax) * 16), ey = (int)((by - ay) * 16);
    int64_t len2 = (int64_t)ex * ex + (int64_t)ey * ey;
    int64_t inv = len2 ? (1ll << 30) / len2 : 0;   /* 픽셀마다 나누지 않도록 역수를 미리 구함 */
    int in2 = R > 8 ? (R - 8) * (R - 8) : 0, out2 = (R + 8) * (R + 8);
    for (int y = y0; y <= y1; y++) {
        int vy = y * 16 + 8 - A_y;
        for (int x = x0; x <= x1; x++) {
            int vx = x * 16 + 8 - A_x, dx, dy;
            int64_t dot = (int64_t)vx * ex + (int64_t)vy * ey;
            if (len2 == 0 || dot <= 0) {
                dx = vx; dy = vy;
            } else if (dot >= len2) {
                dx = vx - ex; dy = vy - ey;
            } else {
                int64_t t = dot * inv;              /* 선분 위 위치 (1 << 30 = 끝) */
                dx = vx - (int)((ex * t) >> 30);
                dy = vy - (int)((ey * t) >> 30);
            }
            int d2 = dx * dx + dy * dy;
            if (d2 >= out2) continue;
            cov_max(x, y, d2 <= in2 ? 255 : (R + 8 - (int)isqrt(d2)) * 255 / 16);
        }
    }
}

static void poly(const float *pts, int n, float r)
{
    for (int i = 0; i + 1 < n; i++) {
        seg(pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], r);
    }
}

/*
 * 타원. r > 0 이면 굵기 r 테두리, r == 0 이면 채움. clip_top 보다 위는 그리지 않음.
 * 정수 연산 (1/16 픽셀 단위). 테두리 근처 픽셀만 거리를 정밀하게 계산한다.
 */
static void ellipse(float cx, float cy, float rx, float ry, float r, float clip_top)
{
    if (rx < 0.3f) rx = 0.3f;
    if (ry < 0.3f) ry = 0.3f;
    int x0 = (int)floorf(cx - rx - r - 1), x1 = (int)ceilf(cx + rx + r + 1);
    int y0 = (int)floorf(fmaxf(cy - ry - r - 1, clip_top - 1)), y1 = (int)ceilf(cy + ry + r + 1);
    if (!clip_box(&x0, &y0, &x1, &y1)) return;

    int C_x = (int)(cx * 16), C_y = (int)(cy * 16), top = (int)(clip_top * 16);
    int64_t Rx = (int)(rx * 16), Ry = (int)(ry * 16), Rl = (int)(r * 16);
    int64_t E = Rx * Ry, Rx2 = Rx * Rx, Ry2 = Ry * Ry, Rx4 = Rx2 * Rx2, Ry4 = Ry2 * Ry2;
    /* 테두리에서 (선 반굵기 + 1.5 픽셀) 이상 떨어진 곳은 계산 없이 판단 */
    int64_t mi = Rx < Ry ? Rx : Ry, band = Rl + 24;
    int64_t lo = mi > band ? E * (mi - band) / mi : 0, hi = E * (mi + band) / mi;
    int64_t lo2 = lo * lo, hi2 = hi * hi;

    for (int y = y0; y <= y1; y++) {
        int py = y * 16 + 8 - C_y;
        if (y * 16 + 8 < top) continue;
        for (int x = x0; x <= x1; x++) {
            int px = x * 16 + 8 - C_x;
            int64_t n = (int64_t)px * px * Ry2 + (int64_t)py * py * Rx2;
            if (n >= hi2) continue;                       /* 바깥 */
            if (n <= lo2) {                               /* 안쪽 */
                if (Rl == 0) cov_max(x, y, 255);
                continue;
            }
            /* 테두리까지 거리 (안쪽 -) : (n - E^2) / |기울기| */
            int64_t g = (int64_t)isqrt64((uint64_t)((int64_t)px * px * Ry4 + (int64_t)py * py * Rx4));
            int d = g > 0 ? (int)((n - E * E) / (2 * g)) : -(int)mi;
            int a = Rl > 0 ? (int)(Rl + 8 - (d < 0 ? -d : d)) : 8 - d;
            cov_max(x, y, a * 255 / 16);
        }
    }
}

static uint16_t blend(uint16_t d, uint16_t s, int a)
{
    int dr = d >> 11, dg = (d >> 5) & 0x3F, db = d & 0x1F;
    int sr = s >> 11, sg = (s >> 5) & 0x3F, sb = s & 0x1F;
    dr += (sr - dr) * a / 255;
    dg += (sg - dg) * a / 255;
    db += (sb - db) * a / 255;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

/* 지금까지 그린 선을 color 로 화면에 칠하고 작업 버퍼를 비움 */
static void flush(uint16_t color)
{
    for (int y = s_by0; y <= s_by1; y++) {
        for (int x = s_bx0; x <= s_bx1; x++) {
            uint8_t *c = &s_cov[y * KAO_W + x];
            if (*c) {
                s_fb[y * KAO_W + x] = *c == 255 ? color : blend(s_fb[y * KAO_W + x], color, *c);
                *c = 0;
            }
        }
    }
    s_bx0 = s_by0 = 1 << 20;
    s_bx1 = s_by1 = -1;
}

/* ---------- 얼굴 부품 (설계 단위) ---------- */

#define PX(u) (s_ox + (u) * SC)
#define PY(v) (s_oy + (v) * SC)

static void line_u(float ax, float ay, float bx, float by, float w)
{
    seg(PX(ax), PY(ay), PX(bx), PY(by), w * SC);
}

/* 눈 (ex: 눈 중심 x, side: -1 왼쪽, +1 오른쪽). 세로 좌표는 깜빡임 비율 s_sy 를 곱한다 */
static void draw_eye(int type, float ex, int side)
{
    float sy = s_sy, w = LW;
#define EX(u) PX(ex + (u))
#define EY(v) PY((v) * sy)

    if (sy < 0.3f && type != E_LINE && type != E_BAR) {
        line_u(ex - 5, 0, ex + 5, 0, w);   /* 감은 눈 */
        return;
    }
    switch (type) {
    case E_DOT:
        ellipse(EX(s_lx * 2.5f), EY(s_ly * 2), 3.2f * SC, 3.8f * SC * sy, 0, NONE);
        break;
    case E_BIGDOT:
        ellipse(EX(s_lx * 1.5f), EY(0), 5 * SC, 5 * SC * sy, 0, NONE);
        break;
    case E_CHEVRON: {
        /* 왼쪽 눈은 >, 오른쪽 눈은 < . 통통 튀듯 크기가 변함 */
        float k = 1 + 0.08f * sinf(s_t * 0.0105f);
        float arm = ex + side * 4.5f * k, tip = ex - side * 4.5f * k;
        float pts[] = { PX(arm), EY(-5 * k), PX(tip), EY(0), PX(arm), EY(5 * k) };
        poly(pts, 3, w * SC);
        break;
    }
    case E_RING:
        ellipse(EX(0), EY(0), 5.5f * SC, 5.5f * SC * sy, w * SC, NONE);
        break;
    case E_SPIRAL: {
        /* 가운데에서 바깥으로 2.5 바퀴, 계속 돈다 (양쪽 반대 방향) */
        float pts[2 * 41];
        float phase = s_t * 0.006f * side;
        for (int i = 0; i <= 40; i++) {
            float th = i * (2.5f * 2 * (float)M_PI / 40);
            float rr = 6.0f * i / 40;
            pts[i * 2] = EX(rr * cosf(th * side + phase));
            pts[i * 2 + 1] = EY(rr * sinf(th * side + phase));
        }
        poly(pts, 41, w * 0.85f * SC);
        break;
    }
    case E_T:
        line_u(ex - 5.5f, -4.5f * sy, ex + 5.5f, -4.5f * sy, w);
        line_u(ex, -4.5f * sy, ex, 5 * sy, w);
        break;
    case E_LINE:
        line_u(ex - 5.5f, 0, ex + 5.5f, 0, w);
        break;
    case E_BAR:
        line_u(ex - 6, 0, ex + 6, 0, w * 2.2f);
        break;
    case E_ANGRY:
        /* 바깥이 올라간 눈썹 + 작은 눈 */
        line_u(ex + side * 6, -5.5f, ex - side * 3.5f, -1.5f, w * 1.2f);
        ellipse(EX(-side * 0.5f + s_lx * 1.5f), EY(2.5f), 2.6f * SC, 2.6f * SC * sy, 0, NONE);
        break;
    case E_TARGET:
        ellipse(EX(0), EY(0), 5.5f * SC, 5.5f * SC * sy, w * SC, NONE);
        ellipse(EX(s_lx * 2), EY(s_ly * 2), 2.4f * SC, 2.4f * SC * sy, 0, NONE);
        break;
    case E_SIDE:
        ellipse(EX(0), EY(0), 5.5f * SC, 5.5f * SC * sy, w * SC, NONE);
        ellipse(EX(2.2f + s_lx * 0.8f), EY(s_ly * 1.5f), 2.8f * SC, 2.8f * SC * sy, 0, NONE);
        break;
    }
#undef EX
#undef EY
}

/* 반원 모양 호 (가운데 (cx, cy), 각도 a0 ~ a1, 0 = 오른쪽, 아래가 +) */
static int arc_pts(float *pts, float cx, float cy, float rx, float ry, float a0, float a1, int n)
{
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        pts[i * 2] = PX(cx + rx * cosf(a));
        pts[i * 2 + 1] = PY(cy + ry * sinf(a));
    }
    return n + 1;
}

/* 입 안쪽을 채우는 색 단계 (pass 0) 와 선 단계 (pass 1) 로 나눠 그린다 */
static void draw_mouth(int type, int pass)
{
    float mx = s_lx * 1.5f, my = MOUTH_Y, w = LW;
    float pts[2 * 32];
    int n;
    switch (type) {
    case M_OMEGA: {
        /* ω : 아래로 볼록한 반원 두 개. 가끔 오물거림 */
        if (pass == 0) break;
        float k = s_t < s_chew_until ? 1 + 0.3f * sinf(s_t * 0.025f) : 1;
        n = arc_pts(pts, mx - 2.6f, my, 2.6f, 2.6f * k, (float)M_PI, 0, 10);
        poly(pts, n, w * SC);
        n = arc_pts(pts, mx + 2.6f, my, 2.6f, 2.6f * k, (float)M_PI, 0, 10);
        poly(pts, n, w * SC);
        break;
    }
    case M_SMILE:
        if (pass == 0) break;
        n = arc_pts(pts, mx, my - 3, 4, 4, 0.25f * (float)M_PI, 0.75f * (float)M_PI, 12);
        poly(pts, n, w * SC);
        break;
    case M_O:
        if (pass == 0) break;
        ellipse(PX(mx), PY(my + 1), 1.8f * SC, (2.3f + 0.3f * sinf(s_t * 0.008f)) * SC, w * SC, NONE);
        break;
    case M_OPEN:
        /* D 를 눕힌 모양: 위는 직선, 아래는 반원, 안은 붉은색 */
        if (pass == 0) {
            ellipse(PX(mx), PY(my - 1), 4 * SC, 4 * SC, 0, PY(my - 1));
            flush(C_MOUTH);
        } else {
            line_u(mx - 4, my - 1, mx + 4, my - 1, w);
            n = arc_pts(pts, mx, my - 1, 4, 4, 0, (float)M_PI, 14);
            poly(pts, n, w * SC);
        }
        break;
    case M_TONGUE:
        /* 웃는 입 + 아래로 내민 혀 */
        if (pass == 0) {
            ellipse(PX(mx + 1), PY(my + 1.6f), 2 * SC, 2.6f * SC, 0, PY(my + 0.3f));
            flush(C_TONGUE);
        } else {
            n = arc_pts(pts, mx, my - 2.5f, 3.8f, 3.2f, 0.15f * (float)M_PI, 0.85f * (float)M_PI, 12);
            poly(pts, n, w * SC);
            ellipse(PX(mx + 1), PY(my + 1.6f), 2 * SC, 2.6f * SC, w * 0.7f * SC, PY(my + 0.6f));
        }
        break;
    case M_WAVY:
        if (pass == 0) break;
        n = 0;
        for (float x = -6; x <= 6.01f; x += 0.75f, n++) {
            pts[n * 2] = PX(mx + x);
            pts[n * 2 + 1] = PY(my + 1.2f * sinf(x * 1.1f + s_t * 0.008f));
        }
        poly(pts, n, w * SC);
        break;
    case M_FLAT:
        if (pass == 0) break;
        line_u(mx - 3.5f, my + 1, mx + 3.5f, my + 1, w);
        break;
    case M_YAWN: {
        /* 하품: 입이 세로로 크게 벌어졌다가 닫힘 */
        float p = (s_t - s_expr_start) / (float)YAWN_MS;
        float open = p < 1 ? sinf(p * (float)M_PI) : 0;
        float rx = 2 + 1.3f * open, ry = 0.8f + 4.5f * open;
        if (pass == 0) {
            ellipse(PX(mx), PY(my + ry * 0.6f), rx * SC, ry * SC, 0, NONE);
            flush(C_MOUTH);
        } else {
            ellipse(PX(mx), PY(my + ry * 0.6f), rx * SC, ry * SC, w * SC, NONE);
        }
        break;
    }
    case M_ZIGZAG: {
        if (pass == 0) break;
        float z[] = { PX(mx - 4.5f), PY(my + 1), PX(mx - 2.2f), PY(my - 1.2f), PX(mx), PY(my + 1),
                      PX(mx + 2.2f), PY(my - 1.2f), PX(mx + 4.5f), PY(my + 1) };
        poly(z, 5, w * SC);
        break;
    }
    }
}

/* 볼 터치 /// */
static void draw_blush(float ex)
{
    for (int i = 0; i < 3; i++) {
        float x = ex - 3.2f + i * 2.6f;
        line_u(x, 9.5f, x + 1.4f, 7.2f, LW * 0.7f);
    }
}

/* T 눈 아래로 떨어지는 눈물 두 방울 */
static void draw_tears(float ex, int side)
{
    for (int i = 0; i < 2; i++) {
        float p = ((s_t + i * 500 + (side > 0 ? 250 : 0)) % 1000) / 1000.0f;
        float x = ex + side * 2.5f, y = 5 + p * 16;
        ellipse(PX(x), PY(y), 1.3f * SC, 1.9f * SC, 0, NONE);
    }
}

/* 졸린 얼굴 옆에 떠오르는 z 두 개 */
static void draw_zzz(void)
{
    for (int i = 0; i < 2; i++) {
        float p = ((s_t + i * 1100) % 2200) / 2200.0f;
        float s = 2.2f + p * 1.5f;
        float x = EYE_X - 2 + p * 4, y = -8 - p * 12;
        float z[] = { PX(x), PY(y), PX(x + s), PY(y), PX(x), PY(y + s), PX(x + s), PY(y + s) };
        poly(z, 4, LW * 0.7f * SC);
    }
}

/* ---------- 공개 함수 ---------- */

void kao_init(uint16_t *fb, uint8_t *cov, uint32_t seed)
{
    s_fb = fb;
    s_cov = cov;
    s_rng = seed;
    memset(s_cov, 0, KAO_W * KAO_H);
    s_bx0 = s_by0 = 1 << 20;
    s_bx1 = s_by1 = -1;
    s_t = 0;
    s_next_blink = 1500;
    s_next_look = 2000;
    s_next_auto = 4000;
    s_next_chew = 1000;
}

int kao_count(void) { return NUM_EXPR; }
const char *kao_name(int idx) { return s_expr[idx].name; }
int kao_current(void) { return s_next >= 0 ? s_next : s_cur; }

int kao_find(const char *name)
{
    for (int i = 0; i < NUM_EXPR; i++) {
        if (strcmp(name, s_expr[i].name) == 0) return i;
    }
    return -1;
}

void kao_set_expr(int idx)
{
    if (idx < 0 || idx >= NUM_EXPR || idx == kao_current()) return;
    s_next = idx;
    s_blink_t = 0;
}

void kao_set_invert(bool on)
{
    s_invert = on;
}

bool kao_get_invert(void)
{
    return s_invert;
}

void kao_set_auto(bool on)
{
    s_auto = on;
    s_next_auto = s_t + 3000;
}

void kao_blink(void)
{
    if (s_blink_t < 0) s_blink_t = 0;
}

void kao_set_offset(float dy)
{
    s_offset = dy;
}

void kao_look(float x, float y)
{
    s_ltx = x;
    s_lty = y;
    s_next_look = s_t + 3000;
}

void kao_update(int dt_ms)
{
    s_t += dt_ms;

    if (s_blink_t >= 0) {
        s_blink_t += dt_ms;
        if (s_next >= 0 && s_blink_t >= BLINK_MS / 2) {
            s_cur = s_next;     /* 눈을 다 감았을 때 표정을 바꿈 */
            s_expr_start = s_t;
            s_next = -1;
        }
        if (s_blink_t >= BLINK_MS) s_blink_t = -1;
    } else if (s_t >= s_next_blink) {
        s_blink_t = 0;
        s_next_blink = s_t + (rnd(0, 4) == 0 ? 350 : rnd(2000, 5000));   /* 가끔 두 번 깜빡 */
    }

    if (s_t >= s_next_look) {
        if (rnd(0, 2) == 0) {
            s_ltx = s_lty = 0;
        } else {
            s_ltx = rnd(-10, 10) / 10.0f;
            s_lty = rnd(-6, 6) / 10.0f;
        }
        s_next_look = s_t + rnd(1200, 3500);
    }
    float k = dt_ms / 150.0f;
    if (k > 1) k = 1;
    s_lx += (s_ltx - s_lx) * k;
    s_ly += (s_lty - s_ly) * k;

    if (s_t >= s_next_chew) {
        s_chew_until = s_t + 700;
        s_next_chew = s_t + rnd(2500, 6000);
    }

    if (s_auto && s_t >= s_next_auto) {
        int idx;
        do {
            idx = rnd(0, NUM_AUTO - 1);  /* 지금과 다른 표정 */
        } while (idx == s_cur);
        kao_set_expr(idx);
        s_next_auto = s_t + rnd(3000, 6000);
    }
}

void kao_render(void)
{
    const int e = s_cur;
    const uint16_t bg = s_invert ? C_DARK : C_PAPER, ink = s_invert ? C_LIGHT : C_INK;
    for (int i = 0; i < KAO_W * KAO_H; i++) s_fb[i] = bg;

    s_sy = s_blink_t >= 0 ? fabsf(1.0f - 2.0f * s_blink_t / BLINK_MS) : 1.0f;
    s_ox = KAO_W / 2 + s_lx * 4;
    s_oy = FACE_Y + s_offset + 1.5f * sinf(s_t * (2 * (float)M_PI / 2600));   /* 숨쉬듯 위아래 */
    if (s_expr[e].extra & X_SHAKE) {
        s_ox += 0.7f * sinf(s_t * 0.09f);                         /* 부들부들 */
    }

    if (s_expr[e].extra & X_BLUSH) {
        draw_blush(-EYE_X);
        draw_blush(EYE_X);
        flush(C_BLUSH);
    }
    draw_mouth(s_expr[e].mouth, 0);

    draw_eye(s_expr[e].eye_l, -EYE_X, -1);
    draw_eye(s_expr[e].eye_r, EYE_X, +1);
    draw_mouth(s_expr[e].mouth, 1);
    if (s_expr[e].extra & X_ZZZ) draw_zzz();
    flush(ink);

    /* 큰 눈동자의 반짝임 */
    if (s_sy >= 0.3f) {
        if (s_expr[e].eye_l == E_BIGDOT) ellipse(PX(-EYE_X - 1.6f + s_lx * 1.5f), PY(-1.6f * s_sy), 1.6f * SC, 1.6f * SC * s_sy, 0, NONE);
        if (s_expr[e].eye_r == E_BIGDOT) ellipse(PX(EYE_X - 1.6f + s_lx * 1.5f), PY(-1.6f * s_sy), 1.6f * SC, 1.6f * SC * s_sy, 0, NONE);
        flush(bg);   /* 반짝임은 바탕색 */
    }
    if (s_expr[e].extra & X_TEARS) {
        draw_tears(-EYE_X, -1);
        draw_tears(EYE_X, +1);
        flush(C_TEAR);
    }
}
