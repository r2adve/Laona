#pragma once
/*
 * 화면 끄기/켜기 연출 시간표 (ESP-IDF 에 의존하지 않음, PC 미리보기에서도 사용)
 *
 *   끌 때 : 하품 -> 잠든 눈 -> 위아래에서 검은 막이 중앙으로 닫힘 -> 끝 (LCD 끄기)
 *   켤 때 : 잠든 눈으로 중앙에서 위아래로 열림 -> 눈을 뜸 -> 끝
 */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *expr;   /* 이번에 바꿀 표정 (없으면 NULL) */
    float shutter;      /* 검은 막이 닫힌 정도 (0 열림 ~ 1 완전히 닫힘) */
    bool done;          /* 연출이 끝남 */
} seq_out_t;

void seq_start_off(void);
void seq_start_on(void);
bool seq_active(void);
bool seq_turning_off(void);
void seq_step(int dt_ms, seq_out_t *out);

/* 화면 위아래에서 shutter 만큼 검게 덮음 (RGB565) */
void seq_draw_shutter(uint16_t *fb, int w, int h, float shutter);
