/*
 * LLM 설정과 설정 페이지 비밀번호 (NVS 에 저장)
 *   비밀번호는 그대로 저장하지 않고 임의의 salt 를 붙인 SHA-256 값만 저장한다.
 */
#include <string.h>
#include "nvs.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "esp_console.h"
#include <stdio.h>
#include "config.h"

#define NS "laona_cfg"
#define DEFAULT_MODEL "claude-opus-5-5"

void config_load(llm_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->provider = LLM_ANTHROPIC;
    strcpy(cfg->model, DEFAULT_MODEL);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t p;
    if (nvs_get_u8(h, "provider", &p) == ESP_OK) cfg->provider = p;
    size_t len = sizeof(cfg->model);
    nvs_get_str(h, "model", cfg->model, &len);
    len = sizeof(cfg->base_url);
    nvs_get_str(h, "base_url", cfg->base_url, &len);
    len = sizeof(cfg->api_key);
    nvs_get_str(h, "api_key", cfg->api_key, &len);
    nvs_close(h);
}

bool config_save(const llm_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_u8(h, "provider", cfg->provider) == ESP_OK &&
              nvs_set_str(h, "model", cfg->model) == ESP_OK &&
              nvs_set_str(h, "base_url", cfg->base_url) == ESP_OK &&
              nvs_set_str(h, "api_key", cfg->api_key) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static const char *PROVIDER_NAMES[LLM_PROVIDER_COUNT] = { "anthropic", "openai", "chatgpt", "gemini" };

const char *config_provider_name(int provider)
{
    return provider >= 0 && provider < LLM_PROVIDER_COUNT ? PROVIDER_NAMES[provider] : "anthropic";
}

int config_provider_find(const char *name)
{
    for (int i = 0; name && i < LLM_PROVIDER_COUNT; i++) {
        if (strcmp(name, PROVIDER_NAMES[i]) == 0) return i;
    }
    return -1;
}

const char *config_base_url(const llm_config_t *cfg)
{
    switch (cfg->provider) {
    case LLM_CHATGPT: return "https://api.openai.com/v1";
    case LLM_GEMINI: return "https://generativelanguage.googleapis.com/v1beta/openai";
    default: return cfg->base_url;
    }
}

bool config_ready(void)
{
    llm_config_t cfg;
    config_load(&cfg);
    if (cfg.model[0] == '\0') return false;
    /* 직접 입력한 OpenAI 호환 API 는 키가 없어도 되는 경우가 있음 (로컬 Ollama 등) */
    return cfg.provider == LLM_OPENAI ? cfg.base_url[0] != '\0' : cfg.api_key[0] != '\0';
}

static void hash(const uint8_t salt[16], const char *password, uint8_t out[32])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, salt, 16);
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

bool config_has_password(void)
{
    nvs_handle_t h;
    size_t len = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_blob(h, "pw_hash", NULL, &len) == ESP_OK && len == 32;
    nvs_close(h);
    return ok;
}

bool config_check_password(const char *password)
{
    nvs_handle_t h;
    uint8_t salt[16], stored[32], calc[32];
    size_t ls = sizeof(salt), lh = sizeof(stored);
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_blob(h, "pw_salt", salt, &ls) == ESP_OK && nvs_get_blob(h, "pw_hash", stored, &lh) == ESP_OK;
    nvs_close(h);
    if (!ok) return false;
    hash(salt, password, calc);
    /* 비교 시간이 내용에 따라 달라지지 않게 */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= calc[i] ^ stored[i];
    return diff == 0;
}

bool config_set_password(const char *password)
{
    uint8_t salt[16], h32[32];
    esp_fill_random(salt, sizeof(salt));
    hash(salt, password, h32);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "pw_salt", salt, sizeof(salt)) == ESP_OK &&
              nvs_set_blob(h, "pw_hash", h32, sizeof(h32)) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void config_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
}

/* 콘솔: setup status | setup reset (USB 로 연결한 사람만 쓸 수 있으므로 비밀번호 없이 허용) */
static int cmd_setup(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        config_reset();
        printf("[Setup] API settings and password erased\n");
        return 0;
    }
    static llm_config_t cfg;
    config_load(&cfg);
    printf("Provider : %s\n", config_provider_name(cfg.provider));
    printf("Model    : %s\n", cfg.model);
    if (cfg.provider != LLM_ANTHROPIC) printf("Base URL : %s\n", config_base_url(&cfg));
    printf("API key  : %s\n", cfg.api_key[0] ? "set" : "(none)");
    printf("Password : %s\n", config_has_password() ? "set" : "(none)");
    memset(&cfg, 0, sizeof(cfg));
    return 0;
}

void config_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "setup",
        .help = "AI settings (set them on the phone: http://laona.local/settings)\n"
                "  setup        : show settings (API key is never shown)\n"
                "  setup reset  : erase API settings and settings password",
        .hint = "[reset]",
        .func = &cmd_setup,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
