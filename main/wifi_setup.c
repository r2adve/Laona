/*
 * 휴대폰으로 와이파이 설정 - SoftAP + 캡티브 포털 + 화면 QR
 *
 *   설정용 와이파이 "Laona-XXXX" (WPA2, 8자리 숫자 비밀번호)
 *     비밀번호를 거는 이유: 휴대폰에서 입력하는 집 와이파이 비밀번호가 공중에 그대로 보이지 않게.
 *     비밀번호는 QR 안에 들어 있어서 사용자가 입력할 일은 거의 없다.
 *     처음 한 번 무작위로 만들어 NVS 에 저장하고 계속 같은 것을 쓴다. 켤 때마다 바꾸면 휴대폰에
 *     지난번 비밀번호로 저장된 같은 이름의 네트워크 때문에 두 번째부터는 접속하지 못한다.
 *   화면 QR
 *     1) WIFI:T:WPA;S:Laona-XXXX;P:12345678;;  -> 휴대폰 카메라로 찍으면 바로 접속
 *     2) http://4.3.2.1/                       -> 휴대폰이 접속하면 자동으로 바뀜 (설정 창이 안 뜰 때)
 *        갤럭시는 카메라 QR 로 접속하면 설정 창 대신 "네트워크에 로그인" 알림만 띄우므로 알림을 누르라고 안내.
 *        설정 > 연결 > Wi-Fi 화면의 QR 스캔 아이콘으로 찍으면 직접 고른 네트워크가 되어 설정 창이 바로 뜬다.
 *     터치하면 1) <-> 2) 전환
 *   DNS  : 모든 질의에 4.3.2.1 로 응답 -> 휴대폰이 로그인 페이지 (설정 페이지) 를 저절로 띄움
 *   HTTP : GET / 설정 페이지, GET /api/scan, POST /api/connect, GET /api/status, 그 외는 / 로
 *
 *   버튼을 3초 눌렀을 때만 켜진다 (touch.c).
 *   설정 와이파이에 접속한 휴대폰이 없는 채로 1분이 지나면 꺼진다 (휴대폰이 접속해 있는 동안은 기다림).
 *   연결되면 QR 을 닫고 기쁜 표정 + "와이파이에 연결됐어요!" 3초, 잠시 뒤 (휴대폰에 결과를 보여준 뒤) 설정 모드를 끄고
 *   대화 페이지 접속 QR 을 띄운다.
 *   연결되지 않은 채로 끝나면 와이파이를 끈다.
 *
 *   주의 (default/ 프로젝트에서 확인): 설정 AP 주소를 192.168.4.1 로 바꾸거나 DHCP 옵션 114 를
 *   넣으면 안드로이드에서 설정 창이 저절로 뜨지 않는다.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "face.h"
#include "laona.h"
#include "web.h"
#include "wifi.h"
#include "wifi_setup.h"

#define IDLE_TIMEOUT_US    (60 * 1000000LL)    /* 접속한 휴대폰이 없으면 이 시간 뒤 종료 */
#define DONE_LINGER_US     (15 * 1000000LL)    /* 연결 성공 후 휴대폰에 결과를 보여줄 시간 */
#define DONE_TEXT_MS       3000                /* 연결 성공 문구 표시 시간 */
#define QR_MS              (60 * 60 * 1000)    /* 표시 시간은 setup_task 가 정함 (끝날 때 닫음) */
#define MAX_SCAN_APS       20
#define NVS_NAMESPACE      "wifi_setup"

extern const char setup_html[] asm("_binary_setup_html_start");

static volatile bool s_running;
static volatile bool s_stop_req;
static volatile bool s_connect_requested;   /* 설정 페이지에서 연결을 요청함 */
static volatile bool s_phone_joined;        /* 휴대폰이 설정 와이파이에 접속함 */
static volatile bool s_page_opened;
static volatile bool s_lingering;           /* 연결 성공, 휴대폰에 결과를 보여주며 끝나기를 기다림 */
static int s_qr_page;                       /* 0: 와이파이 QR, 1: 주소 QR */
static SemaphoreHandle_t s_done;
static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static int s_dns_sock = -1;
static char s_ap_ssid[16], s_ap_pass[9];

static void random_pass(void)
{
    snprintf(s_ap_pass, sizeof(s_ap_pass), "%08lu", (unsigned long)(esp_random() % 100000000));
}

/* 설정 와이파이 비밀번호: 처음 한 번 만들어 저장해 두고 계속 같은 것을 씀 */
static void load_ap_pass(void)
{
    nvs_handle_t nvs;
    size_t len = sizeof(s_ap_pass);
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        random_pass();
        return;
    }
    if (nvs_get_str(nvs, "pass", s_ap_pass, &len) != ESP_OK || strlen(s_ap_pass) != 8) {
        random_pass();
        nvs_set_str(nvs, "pass", s_ap_pass);
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

/* ---------------------------------------------------------------- 화면 */

static void show_qr(int page)
{
    s_qr_page = page;
    if (page == 0) {
        char text[64], line2[24];
        snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", s_ap_ssid, s_ap_pass);
        snprintf(line2, sizeof(line2), "암호 %s", s_ap_pass);
        face_show_qr(text, "QR을 찍어 연결해요", line2, QR_MS);
    } else if (s_phone_joined) {
        /* 갤럭시 등 안드로이드는 카메라 QR 로 접속하면 설정 창을 바로 띄우지 않고
         * "네트워크에 로그인" 알림만 띄운다 (와이파이 설정 화면에서 직접 고른 네트워크만 자동으로 띄움) */
        face_show_qr(WIFI_SETUP_URL, "알림을 눌러 주세요", "또는 이 QR을 찍어요", QR_MS);
    } else {
        face_show_qr(WIFI_SETUP_URL, "설정 창이 안 뜨면", "이 QR을 찍어요", QR_MS);
    }
}

void wifi_setup_next_qr(void)
{
    if (s_running && !s_lingering) show_qr(!s_qr_page);
}

/* 휴대폰이 설정 와이파이에 붙으면 다음 단계 QR 로 */
static void ap_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (!s_running) return;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        printf("[Setup] phone joined\n");
        bool first = !s_phone_joined;
        s_phone_joined = true;
        if (first && !s_page_opened) show_qr(1);
    }
}

/* ---------------------------------------------------------------- DNS */

/* 질의를 그대로 응답으로 바꾸고, A 레코드 질의면 ip 를 답으로 붙인다 */
static int dns_build_reply(uint8_t *buf, int len, int max, uint32_t ip)
{
    if (len < 12 || (buf[2] & 0xf8) != 0 || buf[4] != 0 || buf[5] != 1) {
        return -1;      /* 표준 질의(QR=0, OPCODE=0), 질문 1개만 처리 */
    }
    int p = 12;
    while (p < len && buf[p] != 0) {
        if (buf[p] & 0xc0) return -1;
        p += buf[p] + 1;
    }
    p += 1;
    if (p + 4 > len) return -1;
    uint16_t qtype = (buf[p] << 8) | buf[p + 1];
    p += 4;             /* QTYPE, QCLASS. 뒤의 추가 레코드(EDNS 등)는 버림 */

    buf[2] = 0x84 | (buf[2] & 0x01);    /* QR=1, AA=1, RD 유지 */
    buf[3] = 0x80;                      /* RA=1, RCODE=0 */
    memset(buf + 6, 0, 6);              /* ANCOUNT, NSCOUNT, ARCOUNT */

    if (qtype == 1 && p + 16 <= max) {  /* A 레코드 */
        static const uint8_t ans[12] = {
            0xc0, 0x0c,                 /* 이름: 질문의 이름을 가리킴 */
            0x00, 0x01, 0x00, 0x01,     /* TYPE A, CLASS IN */
            0x00, 0x00, 0x00, 0x3c,     /* TTL 60초 */
            0x00, 0x04,                 /* 데이터 길이 4 */
        };
        buf[7] = 1;
        memcpy(buf + p, ans, sizeof(ans));
        memcpy(buf + p + sizeof(ans), &ip, 4);
        p += 16;
    }
    return p;
}

static esp_err_t dns_open(void)
{
    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) return ESP_FAIL;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500 * 1000 };
    setsockopt(s_dns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (bind(s_dns_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------- HTTP */

static void json_escape(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (; *in && o + 7 < len; in++) {
        unsigned char c = *in;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if (c < 0x20) {
            o += snprintf(out + o, len - o, "\\u%04x", c);
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static int hex_value(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10);
}

/* form-urlencoded 디코딩 (한글 SSID 는 %EB%B0%98... 형태로 들어옴) */
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            *o++ = (hex_value(s[1]) << 4) | hex_value(s[2]);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

static esp_err_t root_handler(httpd_req_t *req)
{
    if (!s_page_opened) {
        s_page_opened = true;
        printf("[Setup] setup page opened\n");
        face_say("happy", "휴대폰에서 집 와이파이를 골라 주세요.");
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, setup_html);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    /* 연결 시도 중에는 scan 이 실패하므로 이전 결과를 보여준다 */
    static wifi_ap_record_t aps[MAX_SCAN_APS];
    static int num;
    int n = wifi_scan(aps, MAX_SCAN_APS);
    if (n >= 0) num = n;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    for (int i = 0; i < num; i++) {
        const char *ssid = (const char *)aps[i].ssid;
        bool dup = ssid[0] == '\0' || strcmp(ssid, s_ap_ssid) == 0;   /* 숨김 SSID, 자기 자신 제외 */
        for (int j = 0; j < i && !dup; j++) {
            dup = strcmp(ssid, (const char *)aps[j].ssid) == 0;
        }
        if (dup) continue;  /* 같은 SSID 가 여러 개면 신호가 가장 센 것만 (scan 결과는 RSSI 순) */
        char esc[33 * 6 + 1], item[256];
        json_escape(ssid, esc, sizeof(esc));
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                 first ? "" : ",", esc, aps[i].rssi,
                 aps[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
        httpd_resp_sendstr_chunk(req, item);
        first = false;
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t connect_handler(httpd_req_t *req)
{
    char body[512];
    if (req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "request too long");
    }
    int len = 0;
    while (len < req->content_len) {
        int r = httpd_req_recv(req, body + len, req->content_len - len);
        if (r <= 0) return ESP_FAIL;
        len += r;
    }
    body[len] = '\0';

    char ssid[100] = "", pass[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "password", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);
    if (ssid[0] == '\0' || strlen(ssid) > 32 || strlen(pass) > 64) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid ssid or password");
    }

    s_connect_requested = true;
    face_thinking(true);
    esp_err_t err = wifi_connect(ssid, pass);
    memset(pass, 0, sizeof(pass));
    memset(body, 0, sizeof(body));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, err == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static const char *names[] = {
        [WIFI_CONN_OFF] = "off",
        [WIFI_CONN_IDLE] = "idle",
        [WIFI_CONN_CONNECTING] = "connecting",
        [WIFI_CONN_CONNECTED] = "connected",
        [WIFI_CONN_FAILED] = "failed",
    };
    char ip[16] = "", buf[96];
    wifi_get_ip(ip, sizeof(ip));
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"ip\":\"%s\",\"reason\":%d}",
             names[wifi_get_state()], ip, wifi_get_last_reason());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

/* 휴대폰 OS 의 인터넷 확인 요청 (generate_204, hotspot-detect.html 등) 을 포함한 모든 요청을 설정 페이지로 */
static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", WIFI_SETUP_URL);
    /* iOS 는 리다이렉트만 있고 본문이 없으면 포털로 인식하지 못함 */
    return httpd_resp_sendstr(req, "Redirect to the captive portal");
}

static esp_err_t http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 6;      /* lwIP 소켓 10개 = httpd 내부 3 + DNS 1 + 클라이언트 6 */
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    if (httpd_start(&s_server, &cfg) != ESP_OK) return ESP_FAIL;

    const httpd_uri_t uris[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = root_handler },
        { .uri = "/api/scan",    .method = HTTP_GET,  .handler = scan_handler },
        { .uri = "/api/connect", .method = HTTP_POST, .handler = connect_handler },
        { .uri = "/api/status",  .method = HTTP_GET,  .handler = status_handler },
    };
    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, redirect_handler);

    /* 리다이렉트되는 요청마다 경고가 찍히지 않도록 */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    return ESP_OK;
}

/* ---------------------------------------------------------------- 설정 태스크 */

static void cleanup(void)
{
    httpd_stop(s_server);
    s_server = NULL;
    close(s_dns_sock);
    s_dns_sock = -1;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_running = false;
    s_lingering = false;
    face_hide_qr();
    printf("[Setup] stopped\n");

    if (wifi_get_state() == WIFI_CONN_CONNECTED) {
        /* 설정 중에는 80 포트를 설정 페이지가 쓰고 있어서 미뤄 둔 대화 페이지 시작.
         * 휴대폰이 집 와이파이로 돌아가면 바로 찍을 수 있게 접속 QR 도 띄움 */
        web_start();
        web_show_address();
    } else {
        wifi_off();   /* 연결 안 됨 -> 와이파이를 끄고 얼굴만 */
        printf("[WiFi] off\n");
    }
}

/* DNS 응답을 처리하면서 연결 결과 표시와 자동 종료를 관리 */
static void setup_task(void *arg)
{
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_ap_netif, &ip_info);

    int64_t deadline = esp_timer_get_time() + IDLE_TIMEOUT_US;
    wifi_conn_state_t last = wifi_get_state();
    uint8_t buf[512];

    while (!s_stop_req) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(s_dns_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (len > 0) {
            int out = dns_build_reply(buf, len, sizeof(buf), ip_info.ip.addr);
            if (out > 0) sendto(s_dns_sock, buf, out, 0, (struct sockaddr *)&from, from_len);
        }

        /* 설정 페이지에서 요청한 연결의 결과를 라오나 화면에도 */
        wifi_conn_state_t st = wifi_get_state();
        if (s_connect_requested && st != last) {
            if (st == WIFI_CONN_CONNECTED) {
                face_thinking(false);
                face_say_ms("happy", "와이파이에 연결됐어요!", DONE_TEXT_MS);
            } else if (st == WIFI_CONN_FAILED) {
                face_thinking(false);
                /* 2 (AUTH_EXPIRE), 205 (CONNECTION_FAIL): 비밀번호 확인 전에 공유기가 응답하지 않음 */
                int reason = wifi_get_last_reason();
                face_say("sad", reason == 2 || reason == 205
                                ? "공유기가 응답하지 않아요. 가까이에서 다시 해 주세요."
                                : "연결하지 못했어요. 비밀번호를 확인해 주세요.");
            }
        }
        last = st;

        int64_t now = esp_timer_get_time();
        if (!s_lingering && s_connect_requested && st == WIFI_CONN_CONNECTED) {
            s_lingering = true;
            deadline = now + DONE_LINGER_US;
        }
        if (!s_lingering) {
            /* 휴대폰이 설정 와이파이에 접속해 있거나 연결을 시도하는 중이면 계속 기다림 */
            wifi_sta_list_t sta;
            if (st == WIFI_CONN_CONNECTING ||
                (esp_wifi_ap_get_sta_list(&sta) == ESP_OK && sta.num > 0)) {
                deadline = now + IDLE_TIMEOUT_US;
            }
        }
        if (now > deadline) {
            if (!s_lingering) printf("[Setup] timeout (no phone)\n");
            break;
        }
    }

    cleanup();
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t wifi_setup_start(void)
{
    if (s_running) {
        show_qr(0);
        return ESP_OK;
    }
    esp_err_t err = wifi_init();
    if (err != ESP_OK) return err;
    if (s_done == NULL) {
        s_done = xSemaphoreCreateBinary();
        s_ap_netif = esp_netif_create_default_wifi_ap();
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, ap_event, NULL);
    }
    xSemaphoreTake(s_done, 0);

    /* 대화 페이지 서버가 80 포트를 쓰고 있으면 잠시 멈춤 (설정이 끝나면 다시 시작) */
    web_stop();

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%02X%02X", LAONA_NAME, mac[4], mac[5]);
    load_ap_pass();

    wifi_config_t ap = {
        .ap = {
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)ap.ap.password, s_ap_pass, sizeof(ap.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    err = wifi_start();
    if (err != ESP_OK) return err;

    /* 설정 AP 주소를 기본값 192.168.4.1 대신 4.3.2.1 로 (안드로이드 캡티브 포털 인식) */
    esp_netif_ip_info_t ip = {
        .ip.addr = ESP_IP4TOADDR(4, 3, 2, 1),
        .gw.addr = ESP_IP4TOADDR(4, 3, 2, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
    };
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip);
    esp_netif_dhcps_start(s_ap_netif);
    /* DHCP 옵션 114 (RFC 8910) 는 쓰지 않는다 - http 주소면 안드로이드가 설정 창을 띄우지 않음 */

    if (dns_open() != ESP_OK || http_start() != ESP_OK) {
        printf("[Setup] DNS/HTTP server start failed\n");
        if (s_dns_sock >= 0) {
            close(s_dns_sock);
            s_dns_sock = -1;
        }
        esp_wifi_set_mode(WIFI_MODE_STA);
        return ESP_FAIL;
    }

    s_stop_req = false;
    s_connect_requested = false;
    s_phone_joined = false;
    s_page_opened = false;
    s_lingering = false;
    s_running = true;
    show_qr(0);
    xTaskCreate(setup_task, "wifi_setup", 4096, NULL, 5, NULL);
    printf("[Setup] join WiFi \"%s\" (password %s), then open %s if no page pops up\n",
           s_ap_ssid, s_ap_pass, WIFI_SETUP_URL);
    return ESP_OK;
}

void wifi_setup_stop(void)
{
    if (!s_running) return;
    s_stop_req = true;
    xSemaphoreTake(s_done, pdMS_TO_TICKS(5000));
}

bool wifi_setup_running(void)
{
    return s_running;
}

const char *wifi_setup_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *wifi_setup_ap_pass(void)
{
    return s_ap_pass;
}
