#pragma once

#include "esp_err.h"

/* 얼굴 애니메이션 태스크 시작 (lcd_init 이후 호출) */
esp_err_t face_init(void);

/* 얼굴 그리기를 멈춤 (화면을 다른 용도로 쓸 때). laona 명령으로 다시 시작 */
void face_pause(void);

/* 터치 등에서 쓰는 얼굴 제어. 바뀐 뒤 상태를 돌려줌 */
const char *face_next_style(void);    /* kao <-> eyes, 새 스타일 이름 */
bool face_toggle_invert(void);        /* 반전 켜짐 여부 */
bool face_toggle_screen(void);        /* 화면 켜짐 여부 */
bool face_screen_on(void);

/* 화면에 QR 코드 (text) 와 아래 두 줄 안내를 ms 동안 보여줌. 터치하면 닫힘 */
void face_show_qr(const char *text, const char *line1, const char *line2, int ms);
void face_hide_qr(void);
bool face_qr_shown(void);

/* LLM 대답 표시: 생각 중 표시, 표정 + 아래 두 줄 글자 */
void face_thinking(bool on);
void face_say(const char *emotion, const char *text);

void face_register_commands(void);
