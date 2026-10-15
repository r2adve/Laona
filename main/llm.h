#pragma once
/*
 * LLM 대화 - Laona 가 직접 Claude API (또는 OpenAI 호환 API) 를 호출한다.
 *   대답은 휴대폰이 아니라 Laona 화면에 표정 + 글자로 나온다.
 */
#include <stdbool.h>
#include "esp_err.h"

esp_err_t llm_init(void);

/* 말 걸기. 대답은 비동기로 화면에 나옴.
 * ESP_ERR_INVALID_STATE: 이전 대답을 기다리는 중, ESP_ERR_NOT_FOUND: API 설정 안 됨 */
esp_err_t llm_ask(const char *text);

bool llm_busy(void);
const char *llm_status(void);    /* 마지막 결과: "", "ok", 또는 오류 설명 */
void llm_forget(void);           /* 대화 기억 지우기 */

void llm_register_commands(void);
