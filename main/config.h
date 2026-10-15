#pragma once
/*
 * LLM 설정과 설정 페이지 비밀번호 (NVS 에 저장)
 */
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LLM_ANTHROPIC,       /* Claude API */
    LLM_OPENAI,          /* 직접 입력한 OpenAI 호환 API (Ollama, LM Studio 등) */
    LLM_CHATGPT,         /* OpenAI API (ChatGPT 모델) */
    LLM_GEMINI,          /* Google Gemini API (OpenAI 호환 주소 사용) */
    LLM_PROVIDER_COUNT,
} llm_provider_t;

typedef struct {
    int provider;
    char model[64];
    char base_url[128];  /* LLM_OPENAI 일 때 (예: http://192.168.0.10:11434/v1) */
    char api_key[256];
} llm_config_t;

void config_load(llm_config_t *cfg);           /* 저장된 값이 없으면 기본값 */
bool config_save(const llm_config_t *cfg);
bool config_ready(void);                        /* API 를 부를 수 있게 설정됨 */

/* 서비스 이름 ("anthropic", "openai", "chatgpt", "gemini") <-> 번호. 모르는 이름은 -1 */
const char *config_provider_name(int provider);
int config_provider_find(const char *name);

/* OpenAI 형식으로 부르는 서비스의 API 주소 (/chat/completions 앞부분) */
const char *config_base_url(const llm_config_t *cfg);

bool config_has_password(void);
bool config_check_password(const char *password);
bool config_set_password(const char *password);

/* API 설정과 비밀번호를 모두 지움 (비밀번호를 잊었을 때 USB 콘솔에서) */
void config_reset(void);

void config_register_commands(void);
