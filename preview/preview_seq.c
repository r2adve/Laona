/*
 * PC 미리보기: 화면 끄기/켜기 연출 (main/seq.c + kao.c + eyes.c)
 *   kao 스타일, eyes 스타일 순서로 "켜진 상태 -> 끄기 -> 꺼진 상태 -> 켜기" 를 보여준다.
 *   출력 형식은 preview.c 와 같음 (프레임마다 "이름\n" + RGB888)
 */
#include <stdio.h>
#include <string.h>
#include "kao.h"
#include "eyes.h"
#include "seq.h"

#define FRAME_MS 40

static uint16_t s_fb[KAO_W * KAO_H];
static uint8_t s_cov[KAO_W * KAO_H];

static void emit(const char *name)
{
    static uint8_t rgb[KAO_W * KAO_H * 3];
    for (int i = 0; i < KAO_W * KAO_H; i++) {
        uint16_t c = s_fb[i];
        int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
        rgb[i * 3] = (r << 3) | (r >> 2);
        rgb[i * 3 + 1] = (g << 2) | (g >> 4);
        rgb[i * 3 + 2] = (b << 3) | (b >> 2);
    }
    printf("%s\n", name);
    fwrite(rgb, 1, sizeof(rgb), stdout);
}

static void set_expr(int style, const char *name)
{
    if (style == 0) {
        kao_set_expr(kao_find(strcmp(name, "sleep") == 0 ? "sleepy" : name));
    } else {
        eyes_set_expr(eyes_find(name));
    }
}

/* ms 동안 진행. 연출 중이면 seq 를 적용 */
static void run(int style, int ms, const char *label, bool off_screen)
{
    for (int t = 0; t < ms; t += FRAME_MS) {
        seq_out_t sq = { 0 };
        bool in_seq = seq_active();
        if (in_seq) {
            seq_step(FRAME_MS, &sq);
            if (sq.expr) set_expr(style, sq.expr);
        }
        kao_update(FRAME_MS);
        eyes_update(FRAME_MS);
        if (off_screen) {
            memset(s_fb, 0, sizeof(s_fb));   /* LCD 꺼짐 */
        } else if (style == 0) {
            kao_render();
        } else {
            eyes_render();
        }
        if (in_seq) seq_draw_shutter(s_fb, KAO_W, KAO_H, sq.shutter);
        emit(label);
    }
}

int main(void)
{
    kao_init(s_fb, s_cov, 99);
    eyes_init(s_fb, 99);
    kao_set_auto(false);
    eyes_set_auto(false);
    for (int style = 0; style < 2; style++) {
        const char *st = style == 0 ? "kao" : "eyes";
        char label[64];
        set_expr(style, "normal");
        snprintf(label, sizeof(label), "%s: on", st);
        run(style, 1500, label, false);
        seq_start_off();
        snprintf(label, sizeof(label), "%s: long press -> turning off", st);
        run(style, 3200, label, false);
        snprintf(label, sizeof(label), "%s: screen off", st);
        run(style, 1000, label, true);
        seq_start_on();
        snprintf(label, sizeof(label), "%s: long press -> turning on", st);
        run(style, 1400, label, false);
        snprintf(label, sizeof(label), "%s: on", st);
        run(style, 1200, label, false);
    }
    return 0;
}
