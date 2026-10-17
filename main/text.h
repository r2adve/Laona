#pragma once
/*
 * 화면 아래 두 줄 글자 영역 (한글 비트맵 글꼴, tools/make_font.py 로 만든 font.bin)
 *   긴 문장은 두 줄씩 나눠 차례로 보여주고, 다 보여준 뒤 잠시 있다가 사라진다.
 *   ESP-IDF 에 의존하지 않음 (PC 미리보기에서도 사용)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEXT_AREA_H  34       /* 아래 글자 영역 높이 (px) */

bool text_init(const uint8_t *font, size_t len);   /* 글꼴 형식이 맞지 않으면 false */

void text_show(const char *utf8);    /* 문장 표시 시작 */
void text_show_ms(const char *utf8, int ms);   /* 짧은 문장 (두 줄 이내) 을 ms 동안만 표시 */
void text_thinking(bool on);         /* "· · ·" 생각 중 표시 */
void text_clear(void);
bool text_active(void);              /* 글자 영역을 쓰는 중 */

void text_update(int dt_ms);
/* 한 줄을 y 위치에 가운데 정렬로 바로 그림 (위의 문장 표시와 상관없음) */
void text_draw_line(uint16_t *fb, int w, int h, int y, const char *utf8, uint16_t fg, uint16_t bg);

/* fb (w x h, RGB565) 아래쪽에 글자 영역을 그림 */
void text_render(uint16_t *fb, int w, int h, uint16_t fg, uint16_t bg);
