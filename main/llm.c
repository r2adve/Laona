/*
 * LLM 대화 - Laona 가 직접 API 를 호출 (별도 서버 없음)
 *
 *   Claude API : POST https://api.anthropic.com/v1/messages
 *                structured outputs 로 {"emotion", "text"} 형식의 대답을 받는다.
 *   OpenAI 형식 : POST {base_url}/chat/completions
 *                ChatGPT (api.openai.com), Gemini (Google 의 OpenAI 호환 주소),
 *                직접 입력한 주소 (Ollama, LM Studio 등)
 *                형식은 지시문으로 요청하고 대답에서 JSON 을 찾아 읽는다.
 *
 *   최근 대화 몇 턴만 기억한다 (RAM, 전원을 끄면 사라짐).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include "config.h"
#include "face.h"
#include "laona.h"
#include "llm.h"

#define LLM_MAX_INPUT     300         /* 사용자 메시지 최대 길이 (바이트) */
#define MAX_TURNS     12          /* 기억하는 메시지 수 (사용자 + Laona) */
#define RESP_MAX      (16 * 1024) /* API 응답 최대 크기 */
#define MAX_TOKENS    1024        /* 대답은 화면 두 줄씩 짧게. 응답이 커지지 않도록 제한 */

/* kao / eyes 표정 이름 (face.c 가 스타일에 맞게 바꿈) */
static const char *EMOTIONS[] = {
    "normal", "happy", "surprised", "dizzy", "cry", "sad", "wink",
    "sleepy", "angry", "stare", "tongue", "meh", "glance",
};
#define NUM_EMOTIONS (sizeof(EMOTIONS) / sizeof(EMOTIONS[0]))

static const char *SYSTEM_PROMPT =
    "너는 '" LAONA_NAME "(" LAONA_NAME_KO ")'라는 작은 책상 로봇이야. 사람들은 휴대폰으로 너에게 말을 걸고, "
    "너의 대답은 너의 작은 화면(가로 160픽셀)에 두 줄씩 글자로 나오고, 얼굴 표정도 함께 바뀌어.\n"
    "대화 상대는 어린이일 수 있어.\n"
    "규칙:\n"
    "- 스스로를 '라오나'라고 불러.\n"
    "- 한국어로, 다정하고 귀여운 해요체로 말해.\n"
    "- 대답은 1~2문장, 40자 안팎으로 짧게 해. 이모지, 마크다운, 목록은 쓰지 마.\n"
    "- 어린이에게 맞지 않는 내용(폭력, 성적인 내용, 무서운 이야기 등)은 다루지 말고 부드럽게 다른 이야기로 바꿔.\n"
    "- 이름, 주소, 학교, 전화번호 같은 개인 정보는 묻지 말고, 알려 줘도 기억하거나 되풀이하지 마.\n"
    "- 위험한 일이나 아프고 힘든 이야기에는 부모님이나 믿을 수 있는 어른께 꼭 말하라고 권해.\n"
    "- emotion 에는 대답에 가장 어울리는 표정 하나를 골라.";

/* OpenAI 호환 API 는 형식을 강제할 수 없어 지시문으로 요청 */
static const char *JSON_RULE =
    "\n- 반드시 {\"emotion\":\"표정\",\"text\":\"대답\"} 형식의 JSON 한 줄로만 대답해. "
    "표정은 normal, happy, surprised, dizzy, cry, sad, wink, sleepy, angry, stare, tongue, meh, glance 중 하나.";

/* server-side fallbacks 를 쓰는 모델 (그 밖의 모델에 보내면 거절될 수 있음) */
static const char *FALLBACK_MODELS[] = { "claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5" };

typedef struct {
    bool user;
    char *text;      /* 사용자 말 또는 Laona 대답 (JSON 그대로) */
} turn_t;

static turn_t s_hist[MAX_TURNS];
static int s_nhist;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static volatile bool s_busy;
static char s_status[96];

/* ---------- 대화 기억 ---------- */

static void hist_drop_oldest(void)
{
    free(s_hist[0].text);
    memmove(s_hist, s_hist + 1, sizeof(turn_t) * (s_nhist - 1));
    s_nhist--;
}

static void hist_add(bool user, const char *text)
{
    if (s_nhist == MAX_TURNS) hist_drop_oldest();
    s_hist[s_nhist].user = user;
    s_hist[s_nhist].text = strdup(text);
    s_nhist++;
    /* 대화는 항상 사용자 말로 시작해야 함 */
    while (s_nhist > 0 && !s_hist[0].user) hist_drop_oldest();
}

void llm_forget(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (s_nhist > 0) hist_drop_oldest();
    xSemaphoreGive(s_lock);
}

static void add_history(cJSON *messages, const char *input)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_nhist; i++) {
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", s_hist[i].user ? "user" : "assistant");
        cJSON_AddStringToObject(m, "content", s_hist[i].text);
        cJSON_AddItemToArray(messages, m);
    }
    xSemaphoreGive(s_lock);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", input);
    cJSON_AddItemToArray(messages, m);
}

/* ---------- HTTP ---------- */

typedef struct {
    const char *name, *value;
} header_t;

/* 성공하면 응답 본문 (호출한 쪽에서 free), 실패하면 NULL. *status 에 HTTP 상태 (연결 실패면 0) */
static char *http_post(const char *url, const header_t *headers, int nheaders, const char *body, int *status)
{
    *status = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    for (int i = 0; i < nheaders; i++) {
        esp_http_client_set_header(c, headers[i].name, headers[i].value);
    }

    char *resp = NULL;
    int len = strlen(body);
    int64_t t0 = esp_timer_get_time();
    if (esp_http_client_open(c, len) != ESP_OK || esp_http_client_write(c, body, len) != len) {
        goto done;
    }
    int64_t t1 = esp_timer_get_time();
    esp_http_client_fetch_headers(c);
    *status = esp_http_client_get_status_code(c);
    /* 느려질 때 원인 구분: 연결 (DNS + TLS) / 서버가 대답을 만드는 시간 / 요청 크기 / 남은 메모리 */
    printf("[LLM] connect %lld ms, server %lld ms, request %d bytes, heap %u (min %u)\n",
           (t1 - t0) / 1000, (esp_timer_get_time() - t1) / 1000, len,
           (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    resp = malloc(RESP_MAX);
    if (!resp) goto done;
    int total = 0, n;
    while (total < RESP_MAX - 1 && (n = esp_http_client_read(c, resp + total, RESP_MAX - 1 - total)) > 0) {
        total += n;
    }
    resp[total] = '\0';
done:
    esp_http_client_cleanup(c);
    return resp;
}

/* ---------- 대답 해석 ---------- */

/* 대답 JSON ({"emotion","text"}) 을 읽음. 형식이 다르면 글 전체를 대답으로 */
static void parse_reply(const char *s, char *emotion, size_t elen, char *text, size_t tlen)
{
    snprintf(emotion, elen, "normal");
    const char *a = strchr(s, '{'), *b = strrchr(s, '}');
    cJSON *j = NULL;
    if (a && b && b > a) {
        char *tmp = strndup(a, b - a + 1);
        j = tmp ? cJSON_Parse(tmp) : NULL;
        free(tmp);
    }
    cJSON *e = j ? cJSON_GetObjectItem(j, "emotion") : NULL;
    cJSON *t = j ? cJSON_GetObjectItem(j, "text") : NULL;
    if (cJSON_IsString(t)) {
        snprintf(text, tlen, "%s", t->valuestring);
        if (cJSON_IsString(e)) {
            for (int i = 0; i < NUM_EMOTIONS; i++) {
                if (strcmp(e->valuestring, EMOTIONS[i]) == 0) snprintf(emotion, elen, "%s", EMOTIONS[i]);
            }
        }
    } else {
        snprintf(text, tlen, "%s", s);
    }
    cJSON_Delete(j);
}

/* HTTP 오류를 화면에 보여줄 짧은 말로 */
static const char *error_text(int status)
{
    switch (status) {
    case 0: return "AI 서버에 연결할 수 없어요. 인터넷이나 API 주소를 확인해 주세요.";
    case 401:
    case 403: return "API 키가 맞지 않아요. 설정을 확인해 주세요.";
    case 404: return "모델 이름을 찾을 수 없어요. 설정을 확인해 주세요.";
    case 429: return "사용량이 다 찼어요. 조금 뒤에 다시 말해 줘요. (무료 키는 내일)";
    default:
        return status >= 500 ? "서버가 바빠요. 잠시 뒤에 다시 말해 줘요." : "말하다가 문제가 생겼어요. 설정을 확인해 주세요.";
    }
}

/* API 오류 응답의 message 를 콘솔에 남김 (키 같은 비밀은 들어 있지 않음) */
static void log_error(int status, const char *resp)
{
    cJSON *j = resp ? cJSON_Parse(resp) : NULL;
    cJSON *err = j ? cJSON_GetObjectItem(j, "error") : NULL;
    cJSON *msg = err ? cJSON_GetObjectItem(err, "message") : NULL;
    printf("[LLM] HTTP %d: %s\n", status, cJSON_IsString(msg) ? msg->valuestring : (resp ? "" : "no response"));
    cJSON_Delete(j);
}

/* ---------- 요청 ---------- */

/* 성공하면 true. 대답 원문 (기억용) 은 *raw 에 (free 필요) */
static bool ask_anthropic(const llm_config_t *cfg, const char *input, char **raw, const char **err)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON_AddNumberToObject(root, "max_tokens", MAX_TOKENS);
    cJSON_AddStringToObject(root, "system", SYSTEM_PROMPT);
    add_history(cJSON_AddArrayToObject(root, "messages"), input);

    /* 짧은 잡담이라 생각은 적게, 대답 형식은 스키마로 강제 */
    cJSON *oc = cJSON_AddObjectToObject(root, "output_config");
    cJSON_AddStringToObject(oc, "effort", "low");
    cJSON *fmt = cJSON_AddObjectToObject(oc, "format");
    cJSON_AddStringToObject(fmt, "type", "json_schema");
    cJSON *schema = cJSON_AddObjectToObject(fmt, "schema");
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");
    cJSON *emo = cJSON_AddObjectToObject(props, "emotion");
    cJSON_AddStringToObject(emo, "type", "string");
    cJSON_AddItemToObject(emo, "enum", cJSON_CreateStringArray(EMOTIONS, NUM_EMOTIONS));
    cJSON_AddStringToObject(cJSON_AddObjectToObject(props, "text"), "type", "string");
    const char *req[] = { "emotion", "text" };
    cJSON_AddItemToObject(schema, "required", cJSON_CreateStringArray(req, 2));
    cJSON_AddFalseToObject(schema, "additionalProperties");

    /* 안전 분류기가 거절하면 서버가 다른 모델로 다시 시도 */
    header_t headers[3] = { { "x-api-key", cfg->api_key }, { "anthropic-version", "2023-06-01" } };
    int nheaders = 2;
    for (int i = 0; i < sizeof(FALLBACK_MODELS) / sizeof(FALLBACK_MODELS[0]); i++) {
        if (strcmp(cfg->model, FALLBACK_MODELS[i]) == 0) {
            cJSON_AddStringToObject(root, "fallbacks", "default");
            headers[nheaders++] = (header_t){ "anthropic-beta", "server-side-fallback-2026-07-01" };
            break;
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) { *err = "메모리가 부족해요."; return false; }
    int status;
    char *resp = http_post("https://api.anthropic.com/v1/messages", headers, nheaders, body, &status);
    free(body);
    if (status != 200) {
        log_error(status, resp);
        *err = error_text(status);
        free(resp);
        return false;
    }

    cJSON *j = cJSON_Parse(resp);
    free(resp);
    cJSON *stop = j ? cJSON_GetObjectItem(j, "stop_reason") : NULL;
    if (cJSON_IsString(stop) && strcmp(stop->valuestring, "refusal") == 0) {
        cJSON_Delete(j);
        *err = "음... 그건 이야기하기 어려워요. 다른 이야기 해요!";
        return false;
    }
    /* 생각 블록 다음에 오는 text 블록이 대답 */
    cJSON *content = j ? cJSON_GetObjectItem(j, "content") : NULL, *block;
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItem(block, "type"), *text = cJSON_GetObjectItem(block, "text");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 && cJSON_IsString(text)) {
            *raw = strdup(text->valuestring);
            break;
        }
    }
    cJSON_Delete(j);
    if (!*raw) { *err = "대답을 읽을 수 없어요."; return false; }
    return true;
}

static bool ask_openai(const llm_config_t *cfg, const char *input, char **raw, const char **err)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    char *prompt = malloc(strlen(SYSTEM_PROMPT) + strlen(JSON_RULE) + 1);
    if (prompt) {
        strcpy(prompt, SYSTEM_PROMPT);
        strcat(prompt, JSON_RULE);
        cJSON_AddStringToObject(sys, "content", prompt);
        free(prompt);
    }
    cJSON_AddItemToArray(messages, sys);
    add_history(messages, input);

    char url[160], auth[280];
    snprintf(url, sizeof(url), "%s/chat/completions", config_base_url(cfg));
    snprintf(auth, sizeof(auth), "Bearer %s", cfg->api_key);
    header_t headers[] = { { "Authorization", auth } };

    /* Gemini 는 기본으로 오래 생각한 뒤 대답해서 느림. 짧은 잡담이라 생각은 적게.
     * 생각 정도를 고를 수 없는 모델이면 400 으로 거절되므로 그때는 빼고 다시 보냄 */
    bool effort = cfg->provider == LLM_GEMINI;
    if (effort) cJSON_AddStringToObject(root, "reasoning_effort", "low");
    int status;
    char *resp = NULL;
    for (;;) {
        char *body = cJSON_PrintUnformatted(root);
        if (!body) { cJSON_Delete(root); *err = "메모리가 부족해요."; return false; }
        resp = http_post(url, headers, cfg->api_key[0] ? 1 : 0, body, &status);
        free(body);
        if (status != 400 || !effort) break;
        log_error(status, resp);
        free(resp);
        printf("[LLM] retry without reasoning_effort\n");
        cJSON_DeleteItemFromObject(root, "reasoning_effort");
        effort = false;
    }
    cJSON_Delete(root);
    if (status != 200) {
        log_error(status, resp);
        *err = error_text(status);
        free(resp);
        return false;
    }

    cJSON *j = cJSON_Parse(resp);
    free(resp);
    cJSON *choices = j ? cJSON_GetObjectItem(j, "choices") : NULL;
    cJSON *msg = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
    cJSON *content = cJSON_GetObjectItem(msg, "content");
    if (cJSON_IsString(content)) {
        *raw = strdup(content->valuestring);
    } else if (cJSON_IsArray(content)) {
        /* 일부 서비스 (Gemini 등) 는 [{"type":"text","text":"..."}] 형태로 줌 */
        size_t len = 1;
        cJSON *part;
        cJSON_ArrayForEach(part, content) {
            cJSON *t = cJSON_GetObjectItem(part, "text");
            if (cJSON_IsString(t)) len += strlen(t->valuestring);
        }
        *raw = calloc(1, len);
        cJSON_ArrayForEach(part, content) {
            cJSON *t = cJSON_GetObjectItem(part, "text");
            if (*raw && cJSON_IsString(t)) strcat(*raw, t->valuestring);
        }
    }
    cJSON_Delete(j);
    if (!*raw) { *err = "대답을 읽을 수 없어요."; return false; }
    return true;
}

static void llm_task(void *arg)
{
    static llm_config_t cfg;
    char *input;
    for (;;) {
        xQueueReceive(s_queue, &input, portMAX_DELAY);
        face_thinking(true);
        config_load(&cfg);

        char *raw = NULL;
        const char *err = NULL;
        int64_t t0 = esp_timer_get_time();
        bool ok = cfg.provider == LLM_ANTHROPIC ? ask_anthropic(&cfg, input, &raw, &err)
                                                : ask_openai(&cfg, input, &raw, &err);
        printf("[LLM] %s in %lld ms\n", ok ? "reply" : "error", (esp_timer_get_time() - t0) / 1000);
        if (ok) {
            char emotion[16], text[400];
            parse_reply(raw, emotion, sizeof(emotion), text, sizeof(text));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            hist_add(true, input);
            hist_add(false, raw);
            xSemaphoreGive(s_lock);
            laona_say("%s", text);
            face_say(emotion, text);
            snprintf(s_status, sizeof(s_status), "ok");
        } else {
            printf("[LLM] %s\n", err);
            face_say("dizzy", err);
            snprintf(s_status, sizeof(s_status), "%s", err);
        }
        free(raw);
        free(input);
        s_busy = false;
    }
}

esp_err_t llm_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(1, sizeof(char *));
    if (!s_lock || !s_queue) return ESP_ERR_NO_MEM;
    /* TLS 와 JSON 처리에 스택이 넉넉히 필요 */
    return xTaskCreate(llm_task, "llm", 10240, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t llm_ask(const char *text)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;
    if (!config_ready()) return ESP_ERR_NOT_FOUND;
    char *copy = strndup(text, LLM_MAX_INPUT);
    if (!copy) return ESP_ERR_NO_MEM;
    s_busy = true;
    s_status[0] = '\0';
    xQueueSend(s_queue, &copy, 0);
    return ESP_OK;
}

bool llm_busy(void)
{
    return s_busy;
}

const char *llm_status(void)
{
    return s_status;
}

/* 콘솔: chat <말> */
static int cmd_chat(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: chat <text> | chat forget\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "forget") == 0) {
        llm_forget();
        printf("[LLM] memory cleared\n");
        return 0;
    }
    char buf[LLM_MAX_INPUT + 1] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strlcat(buf, " ", sizeof(buf));
        strlcat(buf, argv[i], sizeof(buf));
    }
    esp_err_t err = llm_ask(buf);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("[LLM] not configured. Open http://<laona ip>/settings on your phone\n");
    } else if (err == ESP_ERR_INVALID_STATE) {
        printf("[LLM] busy\n");
    }
    return err == ESP_OK ? 0 : 1;
}

void llm_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "chat",
        .help = "Talk to " LAONA_NAME " (reply appears on the LCD)\n"
                "  chat <text>   : send a message\n"
                "  chat forget   : clear conversation memory",
        .hint = "<text>",
        .func = &cmd_chat,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
