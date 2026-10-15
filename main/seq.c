/*
 * 화면 끄기/켜기 연출 시간표
 */
#include <stddef.h>
#include "seq.h"

/* 끌 때 (ms) */
#define OFF_YAWN     0        /* 하품 시작 */
#define OFF_SLEEP    1700     /* 잠든 눈 */
#define OFF_CLOSE    2700     /* 막이 닫히기 시작 */
#define OFF_END      3200     /* 다 닫힘 */
/* 켤 때 (ms) */
#define ON_OPEN_END  500      /* 막이 다 열림 */
#define ON_WAKE      900      /* 눈을 뜸 */
#define ON_END       1400

static enum { IDLE, TURN_OFF, TURN_ON } s_mode;
static int s_t;

void seq_start_off(void) { s_mode = TURN_OFF; s_t = -1; }
void seq_start_on(void) { s_mode = TURN_ON; s_t = -1; }
bool seq_active(void) { return s_mode != IDLE; }
bool seq_turning_off(void) { return s_mode == TURN_OFF; }

/* 처음엔 천천히, 끝에서 빨라지거나 (닫힐 때) 반대로 (열릴 때) */
static float ease_in(float p) { return p * p; }
static float ease_out(float p) { return 1 - (1 - p) * (1 - p); }

/* prev < at <= now 이면 그 시점을 지난 것 */
static bool passed(int prev, int now, int at) { return prev < at && at <= now; }

void seq_step(int dt_ms, seq_out_t *out)
{
    int prev = s_t;
    s_t = s_t < 0 ? 0 : s_t + dt_ms;
    out->expr = NULL;
    out->shutter = 0;
    out->done = false;

    if (s_mode == TURN_OFF) {
        if (passed(prev, s_t, OFF_YAWN)) out->expr = "yawn";
        if (passed(prev, s_t, OFF_SLEEP)) out->expr = "sleep";
        if (s_t >= OFF_CLOSE) {
            float p = (float)(s_t - OFF_CLOSE) / (OFF_END - OFF_CLOSE);
            out->shutter = ease_in(p > 1 ? 1 : p);
        }
        if (s_t >= OFF_END) {
            out->shutter = 1;
            out->done = true;
            s_mode = IDLE;
        }
    } else if (s_mode == TURN_ON) {
        if (passed(prev, s_t, 0)) out->expr = "sleep";
        if (passed(prev, s_t, ON_WAKE)) out->expr = "normal";
        float p = (float)s_t / ON_OPEN_END;
        out->shutter = p >= 1 ? 0 : 1 - ease_out(p);
        if (s_t >= ON_END) {
            out->done = true;
            s_mode = IDLE;
        }
    }
}

void seq_draw_shutter(uint16_t *fb, int w, int h, float shutter)
{
    int bar = (int)(shutter * (h / 2) + 0.5f);   /* 위아래 막 각각의 높이 */
    if (bar <= 0) return;
    if (bar > h / 2) bar = h / 2;
    for (int y = 0; y < bar; y++) {
        for (int x = 0; x < w; x++) {
            fb[y * w + x] = 0;
            fb[(h - 1 - y) * w + x] = 0;
        }
    }
}
