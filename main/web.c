/*
 * 휴대폰용 웹서버
 *
 *   GET  /               대화 페이지          GET  /settings        설정 페이지
 *   GET  /api/state      상태                 POST /api/chat        {"text"} 말 걸기
 *   POST /api/password   {"old","new"}        설정 비밀번호 만들기/바꾸기
 *   GET  /api/config     설정 읽기 (*)        POST /api/config      설정 저장 (*)
 *   POST /api/forget     대화 기억 지우기 (*)
 *   (*) X-Laona-Password 헤더에 설정 비밀번호가 맞아야 함. API 키는 절대 돌려주지 않는다.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "mdns.h"
#include "cJSON.h"
#include "config.h"
#include "laona.h"
#include "llm.h"
#include "web.h"
#include "wifi.h"
#include "wifi_setup.h"
#include "face.h"

#define BODY_MAX 1024

/* gzip 으로 줄인 페이지 (main/CMakeLists.txt) */
extern const uint8_t chat_gz_start[] asm("_binary_chat_html_gz_start");
extern const uint8_t chat_gz_end[] asm("_binary_chat_html_gz_end");
extern const uint8_t settings_gz_start[] asm("_binary_settings_html_gz_start");
extern const uint8_t settings_gz_end[] asm("_binary_settings_html_gz_end");

static httpd_handle_t s_server;

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s ? s : "{}");
    free(s);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", msg);
    return send_json(req, status, j);
}

/* 요청 본문을 JSON 으로 읽음 (실패하면 NULL) */
static cJSON *read_json(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= BODY_MAX) return NULL;
    char buf[BODY_MAX];
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) return NULL;
        got += n;
    }
    buf[got] = '\0';
    return cJSON_Parse(buf);
}

static const char *json_str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* 설정 비밀번호 확인. 틀리면 1초 늦게 거절해서 마구 시도하기 어렵게 함 */
static bool authorized(httpd_req_t *req)
{
    char pw[80] = "";
    if (httpd_req_get_hdr_value_str(req, "X-Laona-Password", pw, sizeof(pw)) == ESP_OK &&
        config_has_password() && config_check_password(pw)) {
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    send_error(req, "401 Unauthorized", "비밀번호가 맞지 않아요.");
    return false;
}

static esp_err_t send_page(httpd_req_t *req, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t page_chat(httpd_req_t *req)
{
    /* 휴대폰이 대화창에 접속하면 접속 QR 을 닫고 바로 얼굴로 */
    if (face_qr_shown()) {
        face_hide_qr();
        printf("[Web] chat page opened -> close QR\n");
    }
    return send_page(req, chat_gz_start, chat_gz_end);
}

static esp_err_t page_settings(httpd_req_t *req)
{
    return send_page(req, settings_gz_start, settings_gz_end);
}

static esp_err_t api_state(httpd_req_t *req)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", LAONA_NAME);
    cJSON_AddBoolToObject(j, "configured", config_ready());
    cJSON_AddBoolToObject(j, "busy", llm_busy());
    cJSON_AddStringToObject(j, "status", llm_status());
    cJSON_AddBoolToObject(j, "has_password", config_has_password());
    return send_json(req, "200 OK", j);
}

static esp_err_t api_chat(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    const char *text = json_str(j, "text");
    esp_err_t err = text && text[0] ? llm_ask(text) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(j);
    switch (err) {
    case ESP_OK: return send_json(req, "200 OK", cJSON_CreateObject());
    case ESP_ERR_INVALID_STATE: return send_error(req, "409 Conflict", "라오나가 아직 생각 중이에요.");
    case ESP_ERR_NOT_FOUND: return send_error(req, "412 Precondition Failed", "설정에서 API 키를 먼저 등록해 주세요.");
    default: return send_error(req, "400 Bad Request", "보낼 말을 입력해 주세요.");
    }
}

static esp_err_t api_password(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    const char *old = json_str(j, "old"), *new = json_str(j, "new");
    esp_err_t err;
    if (config_has_password() && !(old && config_check_password(old))) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        err = send_error(req, "401 Unauthorized", "지금 비밀번호가 맞지 않아요.");
    } else if (!new || strlen(new) < 4 || strlen(new) > 64) {
        err = send_error(req, "400 Bad Request", "비밀번호는 4~64자로 정해 주세요.");
    } else {
        err = config_set_password(new) ? send_json(req, "200 OK", cJSON_CreateObject())
                                       : send_error(req, "500 Internal Server Error", "저장하지 못했어요.");
    }
    cJSON_Delete(j);
    return err;
}

static esp_err_t api_config_get(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    static llm_config_t cfg;
    config_load(&cfg);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "provider", config_provider_name(cfg.provider));
    cJSON_AddStringToObject(j, "model", cfg.model);
    cJSON_AddStringToObject(j, "base_url", cfg.base_url);
    /* 키는 알아볼 수 있게 앞뒤 몇 글자만 */
    char hint[24] = "";
    size_t n = strlen(cfg.api_key);
    if (n > 12) {
        snprintf(hint, sizeof(hint), "%.7s…%s", cfg.api_key, cfg.api_key + n - 4);
    } else if (n > 0) {
        strcpy(hint, "••••");
    }
    cJSON_AddStringToObject(j, "key_hint", hint);
    memset(&cfg, 0, sizeof(cfg));
    return send_json(req, "200 OK", j);
}

static esp_err_t api_config_set(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    static llm_config_t cfg;
    cJSON *j = read_json(req);
    const char *provider = json_str(j, "provider"), *model = json_str(j, "model");
    const char *base_url = json_str(j, "base_url"), *key = json_str(j, "api_key");
    esp_err_t err;
    if (config_provider_find(provider) < 0 || !model || !model[0] || strlen(model) >= sizeof(cfg.model) ||
        (base_url && strlen(base_url) >= sizeof(cfg.base_url)) || (key && strlen(key) >= sizeof(cfg.api_key))) {
        err = send_error(req, "400 Bad Request", "입력값을 확인해 주세요.");
    } else {
        config_load(&cfg);
        int prev = cfg.provider;
        cfg.provider = config_provider_find(provider);
        /* 서비스를 바꾸면 이전 서비스의 키는 쓸 수 없으므로 새 키가 없으면 지움 */
        if (cfg.provider != prev && !(key && key[0])) cfg.api_key[0] = '\0';
        strcpy(cfg.model, model);
        strcpy(cfg.base_url, base_url ? base_url : "");
        /* 끝의 / 는 떼어 둠 (주소 뒤에 /chat/completions 를 붙이므로) */
        size_t n = strlen(cfg.base_url);
        while (n > 0 && cfg.base_url[n - 1] == '/') cfg.base_url[--n] = '\0';
        if (key && key[0]) strcpy(cfg.api_key, key);   /* 비워 두면 기존 키 유지 */
        err = config_save(&cfg) ? send_json(req, "200 OK", cJSON_CreateObject())
                                : send_error(req, "500 Internal Server Error", "저장하지 못했어요.");
        llm_forget();
    }
    memset(&cfg, 0, sizeof(cfg));
    cJSON_Delete(j);
    return err;
}

static esp_err_t api_forget(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    llm_forget();
    return send_json(req, "200 OK", cJSON_CreateObject());
}

#define QR_SHOW_MS 60000      /* 휴대폰이 접속하지 않으면 1분 뒤 얼굴로 */

void web_show_address(void)
{
    char ip[16], url[32];
    if (!wifi_get_ip(ip, sizeof(ip))) {
        /* 와이파이 설정은 버튼 3초로만 시작 */
        printf("[Web] WiFi not connected (hold button 3s for WiFi setup)\n");
        return;
    }
    /* laona.local 은 안드로이드에서 안 될 수 있어 IP 주소로 */
    snprintf(url, sizeof(url), "http://%s/", ip);
    face_show_qr(url, "카메라로 찍어 접속하세요", ip, QR_SHOW_MS);
    printf("[Web] %s\n", url);
}

void web_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    printf("[Web] stopped\n");
}

void web_start(void)
{
    if (s_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 10;
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        printf("[Web] start failed\n");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_chat },
        { .uri = "/settings", .method = HTTP_GET, .handler = page_settings },
        { .uri = "/api/state", .method = HTTP_GET, .handler = api_state },
        { .uri = "/api/chat", .method = HTTP_POST, .handler = api_chat },
        { .uri = "/api/password", .method = HTTP_POST, .handler = api_password },
        { .uri = "/api/config", .method = HTTP_GET, .handler = api_config_get },
        { .uri = "/api/config", .method = HTTP_POST, .handler = api_config_set },
        { .uri = "/api/forget", .method = HTTP_POST, .handler = api_forget },
    };
    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    /* http://laona.local 로 찾을 수 있게 (휴대폰에 따라 안 될 수 있어 IP 도 화면에 보여줌) */
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(LAONA_HOSTNAME);
        mdns_instance_name_set(LAONA_NAME);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        /* 이미 IP 를 받은 뒤에 시작했으므로 바로 알림 */
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) mdns_netif_action(sta, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
    }
    printf("[Web] started: http://%s.local\n", LAONA_HOSTNAME);
}
