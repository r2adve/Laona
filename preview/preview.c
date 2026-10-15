/*
 * PC 미리보기: main/kao.c 를 그대로 컴파일해 프레임을 표준출력으로 내보낸다.
 *   ./preview <표정별 ms> [반전 1]  ->  프레임마다 "이름\n" + RGB888 160x128
 */
#include <stdio.h>
#include <stdlib.h>
#include "kao.h"

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

int main(int argc, char **argv)
{
    int per = argc > 1 ? atoi(argv[1]) : 3000;
    kao_init(s_fb, s_cov, 1234);
    kao_set_auto(false);
    kao_set_invert(argc > 2 && atoi(argv[2]));
    for (int e = 0; e < kao_count(); e++) {
        kao_set_expr(e);
        for (int t = 0; t < per; t += FRAME_MS) {
            kao_update(FRAME_MS);
            kao_render();
            emit(kao_name(kao_current()));
        }
    }
    return 0;
}
