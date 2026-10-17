/*
 * PC 미리보기: 와이파이 연결 아이콘 (오른쪽 위)
 *   출력 형식은 preview.c 와 같음
 */
#include <stdio.h>
#include "kao.h"
#include "eyes.h"
#include "icon.h"

#define FRAME_MS 40
#define ICON_X   (KAO_W - ICON_WIFI_W - 3)
#define ICON_Y   3

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

static void run(int style, bool dark, const char *expr, int ms, const char *label)
{
    if (style == 0) kao_set_expr(kao_find(expr)); else eyes_set_expr(eyes_find(expr));
    for (int t = 0; t < ms; t += FRAME_MS) {
        kao_update(FRAME_MS);
        eyes_update(FRAME_MS);
        if (style == 0) kao_render(); else eyes_render();
        icon_draw_wifi(s_fb, KAO_W, KAO_H, ICON_X, ICON_Y, dark ? 0xFFFF : 0x2926);   /* face.c 와 같이 바탕의 반대색 (C_INK) */
        emit(label);
    }
}

int main(void)
{
    kao_init(s_fb, s_cov, 7);
    eyes_init(s_fb, 7);
    kao_set_auto(false);
    eyes_set_auto(false);

    kao_set_invert(true);
    run(0, true, "normal", 1200, "kao dark + wifi");
    run(0, true, "happy", 1200, "kao dark happy + wifi");
    kao_set_invert(false);
    run(0, false, "normal", 1200, "kao light + wifi");
    eyes_set_invert(false);
    run(1, true, "normal", 1200, "eyes dark + wifi");
    eyes_set_invert(true);
    run(1, false, "normal", 1200, "eyes light + wifi");
    return 0;
}
