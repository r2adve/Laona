#pragma once
/*
 * 화면 위에 덧그리는 작은 상태 아이콘 (ESP-IDF 를 쓰지 않아 PC 미리보기에서도 컴파일됨)
 */
#include <stdint.h>

#define ICON_WIFI_W  9
#define ICON_WIFI_H  7

/* fb (w x h RGB565) 의 (x, y) 에 와이파이 아이콘을 color 로 그림 */
void icon_draw_wifi(uint16_t *fb, int w, int h, int x, int y, uint16_t color);
