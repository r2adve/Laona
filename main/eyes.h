#pragma once
/*
 * 흰 둥근 눈 얼굴 - 두 눈이 깜빡이고 두리번거리며 표정을 바꾼다
 *   kao.c 와 같은 방식의 순수 C (fb 는 RGB565, 바이트 순서 그대로)
 */
#include <stdbool.h>
#include <stdint.h>

#define EYES_W 160
#define EYES_H 128

void eyes_init(uint16_t *fb, uint32_t seed);

int eyes_count(void);
const char *eyes_name(int idx);
int eyes_find(const char *name);      /* 없으면 -1 */
int eyes_current(void);

void eyes_set_expr(int idx);
void eyes_set_auto(bool on);
void eyes_set_invert(bool on);        /* 흰 바탕 + 검은 눈 */
void eyes_set_color(uint16_t color);  /* 눈 색 */
void eyes_blink(void);
void eyes_look(float x, float y);     /* -1 ~ 1 */
void eyes_set_offset(float dy);       /* 얼굴 전체를 위아래로 옮김 (px, 위가 -) */

void eyes_update(int dt_ms);
void eyes_render(void);
