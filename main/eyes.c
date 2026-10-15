/*
 * 흰 둥근 눈 얼굴 (처음 만든 형태)
 *   둥근 사각형 눈 두 개. 눈꺼풀 기울기로 화남/슬픔, 아래를 둥글게 깎아 웃음을 표현한다.
 */
#include <string.h>
#include "eyes.h"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define EYE_GAP     64      /* 두 눈 중심 사이 거리 */
#define EYE_RADIUS  12      /* 눈 모서리 둥글기 */
#define EASE        0.3f    /* 매 프레임 목표값에 다가가는 비율 */
#define LOOK_X      22      /* 바라보는 방향으로 움직이는 최대 거리 (px) */
#define LOOK_Y      14

typedef struct {
    float w, h;      /* 눈 크기 */
    float lid;       /* 위 눈꺼풀이 덮는 비율 (0 ~ 1) */
    float slant;     /* 눈꺼풀 기울기 (px). + 는 안쪽이 내려감 (화남), - 는 바깥쪽이 내려감 (슬픔) */
    float happy;     /* 아래에서 둥글게 깎아 ^ ^ 모양 (0 ~ 1) */
} eye_shape_t;

static const struct {
    const char *name;
    eye_shape_t shape;
} s_emotions[] = {
    { "normal",    { 36, 46, 0.00f,   0, 0 } },
    { "happy",     { 40, 44, 0.00f,   0, 1 } },
    { "angry",     { 38, 40, 0.30f,  14, 0 } },
    { "sad",       { 36, 40, 0.30f, -14, 0 } },
    { "sleepy",    { 40, 40, 0.55f,   0, 0 } },
    { "surprised", { 44, 56, 0.00f,   0, 0 } },
    /* 아래는 자동 표정 변경에서 고르지 않음 */
    { "yawn",      { 46, 34, 0.00f,   0, 1.00f } },  /* 질끈 감은 ^ ^ 눈 */
    { "sleep",     { 40, 40, 0.93f,   0, 0 } },      /* 거의 감은 눈 */
};
#define NUM_EMOTIONS ((int)(sizeof(s_emotions) / sizeof(s_emotions[0])))
#define NUM_AUTO     6

/* 눈 깜빡임 단계별 높이 비율 (프레임 40ms 기준) */
static const float s_blink_seq[] = { 0.6f, 0.15f, 0.05f, 0.3f, 0.7f, 1.0f };
#define BLINK_FRAMES ((int)(sizeof(s_blink_seq) / sizeof(s_blink_seq[0])))

static uint16_t *s_fb;
static uint32_t s_rng;
static uint32_t s_t;
static bool s_auto = true;
static bool s_invert;
static uint16_t s_eye_color = RGB(255, 255, 255);
static int s_emotion;
static float s_offset;                 /* 얼굴 위아래 이동 (글자 영역을 피할 때) */

static eye_shape_t s_cur, s_target;
static float s_look_x, s_look_y, s_look_tx, s_look_ty;
static int s_blink = -1;               /* 깜빡임 진행 단계, -1 이면 안 깜빡임 */
static uint32_t s_next_blink, s_next_look, s_next_emotion;

static int rnd(int lo, int hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return lo + (int)((s_rng >> 8) % (uint32_t)(hi - lo + 1));
}

static float ease(float cur, float target)
{
    return cur + (target - cur) * EASE;
}

/* side: -1 왼쪽 눈, +1 오른쪽 눈 */
static void draw_eye(int cx, int cy, int w, int h, int lid, int slant, int cut, int side, uint16_t color)
{
    int l = cx - w / 2, t = cy - h / 2;
    int rad = EYE_RADIUS;
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    int half_w = w / 2 > 0 ? w / 2 : 1;
    int cut_r = w;                          /* ^ 모양을 깎아내는 원 */
    int cut_cy = t + h + cut_r - cut;

    int y0 = t < 0 ? 0 : t, y1 = t + h > EYES_H ? EYES_H : t + h;
    int x0 = l < 0 ? 0 : l, x1 = l + w > EYES_W ? EYES_W : l + w;
    for (int y = y0; y < y1; y++) {
        int dy = y - t;
        int qy = dy < rad ? rad - dy : (dy >= h - rad ? dy - (h - rad - 1) : 0);
        for (int x = x0; x < x1; x++) {
            int dx = x - l;
            int qx = dx < rad ? rad - dx : (dx >= w - rad ? dx - (w - rad - 1) : 0);
            if (qx * qx + qy * qy > rad * rad) {
                continue;   /* 둥근 모서리 바깥 */
            }
            /* 왼쪽 눈은 오른쪽(+x) 이, 오른쪽 눈은 왼쪽이 안쪽 */
            if (y < t + lid + slant * (x - cx) * -side / half_w) {
                continue;   /* 위 눈꺼풀 */
            }
            if (cut > 0) {
                int ex = x - cx, ey = y - cut_cy;
                if (ex * ex + ey * ey < cut_r * cut_r) {
                    continue;
                }
            }
            s_fb[y * EYES_W + x] = color;
        }
    }
}

void eyes_init(uint16_t *fb, uint32_t seed)
{
    s_fb = fb;
    s_rng = seed;
    s_t = 0;
    s_emotion = 0;
    s_target = s_cur = s_emotions[0].shape;
    s_next_blink = 1500;
    s_next_look = 2000;
    s_next_emotion = 6000;
}

int eyes_count(void) { return NUM_EMOTIONS; }
const char *eyes_name(int idx) { return s_emotions[idx].name; }
int eyes_current(void) { return s_emotion; }

int eyes_find(const char *name)
{
    for (int i = 0; i < NUM_EMOTIONS; i++) {
        if (strcmp(name, s_emotions[i].name) == 0) return i;
    }
    return -1;
}

void eyes_set_expr(int idx)
{
    if (idx < 0 || idx >= NUM_EMOTIONS) return;
    s_emotion = idx;
    s_target = s_emotions[idx].shape;
}

void eyes_set_auto(bool on)
{
    s_auto = on;
    s_next_emotion = s_t + 3000;
}

void eyes_set_invert(bool on) { s_invert = on; }
void eyes_set_color(uint16_t color) { s_eye_color = color; }
void eyes_blink(void) { if (s_blink < 0) s_blink = 0; }

void eyes_set_offset(float dy) { s_offset = dy; }

void eyes_look(float x, float y)
{
    s_look_tx = x * LOOK_X;
    s_look_ty = y * LOOK_Y;
    s_next_look = s_t + 3000;
}

/* 깜빡임, 두리번거림, 표정 변화를 시간에 맞춰 일으킨다 */
void eyes_update(int dt_ms)
{
    s_t += dt_ms;
    if (s_blink >= 0) {
        if (++s_blink >= BLINK_FRAMES) {
            s_blink = -1;
        }
    } else if (s_t >= s_next_blink) {
        s_blink = 0;
        /* 가끔 두 번 연달아 깜빡임 */
        s_next_blink = s_t + (rnd(0, 4) == 0 ? 300 : rnd(2000, 6000));
    }

    if (s_t >= s_next_look) {
        if (rnd(0, 2) == 0) {
            s_look_tx = 0;
            s_look_ty = 0;
        } else {
            s_look_tx = rnd(-LOOK_X, LOOK_X);
            s_look_ty = rnd(-LOOK_Y, LOOK_Y);
        }
        s_next_look = s_t + rnd(1000, 4000);
    }

    if (s_auto && s_t >= s_next_emotion) {
        /* 대부분은 평소 표정, 가끔 다른 표정 */
        eyes_set_expr(rnd(0, 2) == 0 ? rnd(1, NUM_AUTO - 1) : 0);
        s_next_emotion = s_t + rnd(4000, 10000);
    }

    s_cur.w = ease(s_cur.w, s_target.w);
    s_cur.h = ease(s_cur.h, s_target.h);
    s_cur.lid = ease(s_cur.lid, s_target.lid);
    s_cur.slant = ease(s_cur.slant, s_target.slant);
    s_cur.happy = ease(s_cur.happy, s_target.happy);
    s_look_x = ease(s_look_x, s_look_tx);
    s_look_y = ease(s_look_y, s_look_ty);
}

void eyes_render(void)
{
    uint16_t bg = s_invert ? s_eye_color : 0, eye = s_invert ? 0 : s_eye_color;
    for (int i = 0; i < EYES_W * EYES_H; i++) s_fb[i] = bg;

    float blink = s_blink >= 0 ? s_blink_seq[s_blink] : 1.0f;
    int lx = EYES_W / 2 - EYE_GAP / 2 + (int)s_look_x;
    int rx = EYES_W / 2 + EYE_GAP / 2 + (int)s_look_x;
    int cy = EYES_H / 2 + (int)(s_look_y + s_offset);
    int w = (int)s_cur.w, h = (int)(s_cur.h * blink);
    if (h < 3) h = 3;
    int lid = (int)(s_cur.lid * h), slant = (int)s_cur.slant, cut = (int)(s_cur.happy * h * 0.6f);

    draw_eye(lx, cy, w, h, lid, slant, cut, -1, eye);
    draw_eye(rx, cy, w, h, lid, slant, cut, +1, eye);
}
