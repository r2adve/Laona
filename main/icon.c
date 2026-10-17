/*
 * 상태 아이콘
 *   와이파이: 흔히 쓰는 부채꼴 (호 두 개 + 점), 9x7
 */
#include "icon.h"

static const char *s_wifi[ICON_WIFI_H] = {
    "..#####..",
    ".#.....#.",
    "#..###..#",
    "..#...#..",
    ".........",
    "....#....",
    ".........",
};

void icon_draw_wifi(uint16_t *fb, int w, int h, int x, int y, uint16_t color)
{
    for (int r = 0; r < ICON_WIFI_H; r++) {
        for (int c = 0; c < ICON_WIFI_W; c++) {
            int px = x + c, py = y + r;
            if (s_wifi[r][c] == '#' && px >= 0 && px < w && py >= 0 && py < h) {
                fb[py * w + px] = color;
            }
        }
    }
}
