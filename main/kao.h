#pragma once
/*
 * 이모티콘(카오모지) 얼굴 - 선으로 그린 눈/입 + 움직임
 *   ESP-IDF 에 의존하지 않는 순수 C 라서 PC 미리보기 (preview/) 에서도 같은 코드를 쓴다.
 */
#include <stdbool.h>
#include <stdint.h>

#define KAO_W 160
#define KAO_H 128

/* fb: KAO_W * KAO_H RGB565 (바이트 순서 그대로), cov: KAO_W * KAO_H 작업용 버퍼 */
void kao_init(uint16_t *fb, uint8_t *cov, uint32_t seed);

int kao_count(void);
const char *kao_name(int idx);
int kao_find(const char *name);       /* 없으면 -1 */
int kao_current(void);

void kao_set_expr(int idx);           /* 눈을 감았다 뜨며 바뀜 */
void kao_set_auto(bool on);           /* 표정을 저절로 바꾸기 */
void kao_set_invert(bool on);         /* 색 반전: 검은 바탕 + 흰 선 */
bool kao_get_invert(void);
void kao_blink(void);
void kao_look(float x, float y);      /* -1 ~ 1 */
void kao_set_offset(float dy);        /* 얼굴 전체를 위아래로 옮김 (px, 위가 -) */

void kao_update(int dt_ms);           /* 시간 진행 (깜빡임, 두리번거림, 애니메이션) */
void kao_render(void);                /* fb 에 현재 얼굴을 그림 */
