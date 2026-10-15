/*
 * PC 미리보기: 대화 표시 (생각 중 -> 표정 + 아래 두 줄 글자)
 *   출력 형식은 preview.c 와 같음
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kao.h"
#include "eyes.h"
#include "text.h"

#define FRAME_MS 40
#define C_INK  0x2926   /* kao.c 의 선 색 (40, 36, 48) */

static uint16_t s_fb[KAO_W * KAO_H];
static uint8_t s_cov[KAO_W * KAO_H];
static float s_dy;

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

static void run(int style, bool dark, int ms, const char *label)
{
    uint16_t fg = dark ? 0xFFFF : C_INK, bg = dark ? 0x0000 : 0xFFFF;
    for (int t = 0; t < ms; t += FRAME_MS) {
        text_update(FRAME_MS);
        float target = text_active() ? -16 : 0;
        s_dy += (target - s_dy) * 0.25f;
        kao_set_offset(s_dy);
        eyes_set_offset(s_dy);
        kao_update(FRAME_MS);
        eyes_update(FRAME_MS);
        if (style == 0) kao_render(); else eyes_render();
        text_render(s_fb, KAO_W, KAO_H, fg, bg);
        emit(label);
    }
}

int main(void)
{
    static uint8_t font[1 << 20];
    FILE *f = fopen("../main/font.bin", "rb");
    size_t n = fread(font, 1, sizeof(font), f);
    fclose(f);
    if (!text_init(font, n)) return 1;

    kao_init(s_fb, s_cov, 7);
    eyes_init(s_fb, 7);
    kao_set_auto(false);
    eyes_set_auto(false);
    kao_set_invert(true);       /* 부팅 기본: 검은 바탕 */
    eyes_set_invert(false);

    run(0, true, 1000, "kao: idle");
    text_thinking(true);
    kao_set_expr(kao_find("glance"));
    run(0, true, 1600, "kao: thinking (waiting for API)");
    kao_set_expr(kao_find("happy"));
    text_show("안녕! 나는 라오나야. 오늘은 어떤 하루였어? 재밌는 일 있었으면 나한테도 알려줘 ㅋㅋ");
    run(0, true, 9000, "kao: reply (happy)");
    run(0, true, 1500, "kao: reply done");

    kao_set_invert(false);
    eyes_set_invert(true);
    text_show("우와, 진짜 대단하다! 나도 같이 보고 싶어 ♥");
    eyes_set_expr(eyes_find("surprised"));
    run(1, false, 4500, "eyes: reply (light background)");
    return 0;
}
