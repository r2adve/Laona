/*
 * WiFi STA - 콘솔 명령으로 필요할 때만 켜고 연결
 *   wifi scan
 *   wifi connect <ssid | scan 번호> [password]
 *   wifi disconnect
 *   wifi status
 *   wifi txpower [dBm]
 *   wifi forget
 *   wifi setup [stop]
 *
 * 연결에 성공한 설정은 NVS 에 저장되어 다음 부팅 때 화면 표시 없이 다시 연결한다.
 * 연결하지 못하거나 (재시도 초과) 쓰는 중에 끊기면 와이파이를 끈다. 설정 QR 은 띄우지 않는다.
 * 휴대폰 설정 모드 (wifi_setup.c) 는 버튼을 3초 눌렀을 때만 켜진다 (touch.c).
 * 연결되어 있는 동안 화면 오른쪽 위에 와이파이 아이콘.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "wifi.h"
#include "laona.h"
#include "face.h"
#include "web.h"
#include "wifi_setup.h"

#define MAX_RETRY      5
#define MAX_SCAN_APS   20
#define NVS_NAMESPACE  "wifi"
/* 송신 세기 상한 (0.25 dBm 단위, 최대 84 = 21 dBm). C3 SuperMini 처럼 안테나가 작은 보드는
 * 최대 세기에서 오히려 패킷이 깨지는 일이 많아 8.5 dBm 으로 낮춰 둔다 (공유기 인증 응답 없음 reason 2/205) */
#define TX_POWER_QDBM  34

static bool s_started;
static bool s_want_connected;   /* 사용자가 연결을 요청한 상태 (끊기면 재시도) */
static bool s_connected;
static int s_retry;
static int s_last_reason;
static esp_netif_t *s_netif;
static wifi_ap_record_t s_aps[MAX_SCAN_APS];   /* 마지막 scan 결과 (번호로 연결할 때 사용) */
static uint16_t s_num_aps;
static int8_t s_tx_power = TX_POWER_QDBM;

static void save_config(void)
{
    wifi_config_t cfg;
    nvs_handle_t nvs;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK ||
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_str(nvs, "ssid", (char *)cfg.sta.ssid);
    nvs_set_str(nvs, "pass", (char *)cfg.sta.password);
    nvs_commit(nvs);
    nvs_close(nvs);
}

/* 공유기가 알려준 DNS 가 응답하지 않을 때를 대비해 예비 DNS 를 넣고, 쓰는 DNS 를 로그로 */
static void setup_dns(void)
{
    esp_netif_dns_info_t dns = { .ip.type = ESP_IPADDR_TYPE_V4 };
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
    esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_FALLBACK, &dns);
    printf("[WiFi] DNS");
    for (int i = ESP_NETIF_DNS_MAIN; i <= ESP_NETIF_DNS_FALLBACK; i++) {
        if (esp_netif_get_dns_info(s_netif, i, &dns) == ESP_OK) {
            printf(" " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        }
    }
    printf("\n");
}

static void off_task(void *arg)
{
    /* 설정 모드가 그사이 켜졌으면 (버튼) 끄지 않음 */
    if (!wifi_setup_running() && !s_want_connected) {
        wifi_off();
        printf("[WiFi] off\n");
    }
    vTaskDelete(NULL);
}

static bool load_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_str(nvs, "ssid", ssid, &ssid_len) == ESP_OK &&
              nvs_get_str(nvs, "pass", pass, &pass_len) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = data;
        s_connected = false;
        face_set_wifi(false);
        /* 다른 AP 로 바꾸려고 직접 끊은 경우 */
        if (e->reason == WIFI_REASON_ASSOC_LEAVE || !s_want_connected) {
            return;
        }
        s_last_reason = e->reason;
        if (s_retry < MAX_RETRY) {
            s_retry++;
            printf("[WiFi] disconnected (reason %d), retry %d/%d\n", e->reason, s_retry, MAX_RETRY);
            esp_wifi_connect();
        } else {
            s_want_connected = false;
            printf("[WiFi] connect failed (reason %d)\n", e->reason);
            /* 연결하지 못하면 와이파이를 끄고 얼굴만 (설정 QR 은 버튼으로만).
             * 설정 모드 중이면 설정 페이지에서 다시 시도할 수 있게 그대로 둠.
             * 이벤트 태스크 안에서 와이파이를 멈추지 않도록 따로 태스크에서 */
            if (!wifi_setup_running()) {
                xTaskCreate(off_task, "wifi_off", 3072, NULL, 5, NULL);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_connected = true;
        s_retry = 0;
        face_set_wifi(true);
        save_config();
        printf("[WiFi] connected, ip " IPSTR "\n", IP2STR(&e->ip_info.ip));
        setup_dns();
        /* 설정 모드 중이면 설정 모드가 끝날 때 웹서버를 시작하고 주소를 보여줌 (같은 80 포트) */
        if (wifi_setup_running()) return;
        web_start();   /* 접속 QR 은 버튼을 한 번 눌렀을 때 (touch.c) */
    }
}

esp_err_t wifi_init(void)
{
    if (s_netif == NULL) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_netif = esp_netif_create_default_wifi_sta();
        esp_netif_set_hostname(s_netif, LAONA_HOSTNAME);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
    return ESP_OK;
}

/* 처음 호출될 때 초기화하고 RF 를 켠다 */
esp_err_t wifi_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    wifi_init();
    esp_err_t err = esp_wifi_start();
    s_started = (err == ESP_OK);
    if (s_started) {
        /* 절전 (비콘 사이에 RF 를 끔) 을 끄면 휴대폰 요청에 바로 응답 (전원은 USB 라 소모 전류보다 반응 우선) */
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_max_tx_power(s_tx_power);
    }
    return err;
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    esp_err_t err = wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    s_want_connected = false;
    esp_wifi_disconnect();

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_retry = 0;
    s_last_reason = 0;
    s_want_connected = true;
    printf("[WiFi] connecting to \"%s\"...\n", ssid);
    esp_wifi_connect();
    return ESP_OK;
}

bool wifi_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t ip;
    if (!s_connected || esp_netif_get_ip_info(s_netif, &ip) != ESP_OK) return false;
    snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    return true;
}

void wifi_off(void)
{
    if (!s_started) {
        return;
    }
    s_want_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_started = false;
    s_connected = false;
    face_set_wifi(false);
}

int wifi_scan(wifi_ap_record_t *aps, uint16_t max)
{
    if (wifi_start() != ESP_OK || esp_wifi_scan_start(NULL, true) != ESP_OK) {
        return -1;
    }
    esp_wifi_scan_get_ap_records(&max, aps);
    return max;
}

wifi_conn_state_t wifi_get_state(void)
{
    if (!s_started) return WIFI_CONN_OFF;
    if (s_connected) return WIFI_CONN_CONNECTED;
    if (s_want_connected) return WIFI_CONN_CONNECTING;
    if (s_last_reason) return WIFI_CONN_FAILED;
    return WIFI_CONN_IDLE;
}

int wifi_get_last_reason(void)
{
    return s_last_reason;
}

bool wifi_has_saved(void)
{
    char ssid[33], pass[65];
    return load_config(ssid, sizeof(ssid), pass, sizeof(pass)) && ssid[0];
}

void wifi_autoconnect(void)
{
    char ssid[33], pass[65];
    if (load_config(ssid, sizeof(ssid), pass, sizeof(pass)) && ssid[0]) {
        wifi_connect(ssid, pass);
    } else {
        printf("[WiFi] no saved WiFi (hold button 3s for WiFi setup)\n");
    }
}

static int cmd_scan(void)
{
    printf("[WiFi] scanning...\n");
    int num = wifi_scan(s_aps, MAX_SCAN_APS);
    if (num < 0) {
        printf("[WiFi] scan failed\n");
        return 1;
    }
    s_num_aps = num;
    printf("%3s  %-32s %4s %3s %s\n", "NO", "SSID", "RSSI", "CH", "AUTH");
    for (int i = 0; i < s_num_aps; i++) {
        printf("%3d  %-32s %4d %3d %s\n", i + 1, (char *)s_aps[i].ssid, s_aps[i].rssi,
               s_aps[i].primary, s_aps[i].authmode == WIFI_AUTH_OPEN ? "open" : "secured");
    }
    return 0;
}

/* 숫자만 입력하면 마지막 scan 결과의 번호로 보고 해당 SSID 로 변환 (한글 SSID 입력용) */
static const char *resolve_ssid(const char *arg)
{
    char *end;
    long no = strtol(arg, &end, 10);
    if (*end == '\0' && no >= 1 && no <= s_num_aps) {
        return (const char *)s_aps[no - 1].ssid;
    }
    return arg;
}

/* 연결을 끊고 RF 까지 끈다 */
static int cmd_disconnect(void)
{
    if (!s_started) {
        printf("[WiFi] already off\n");
        return 0;
    }
    wifi_setup_stop();
    wifi_off();
    printf("[WiFi] off\n");
    return 0;
}

static int cmd_status(void)
{
    char ssid[33], pass[65];
    if (wifi_setup_running()) {
        printf("Setup : AP \"%s\" password %s -> %s\n", wifi_setup_ap_ssid(), wifi_setup_ap_pass(), WIFI_SETUP_URL);
    }
    printf("Saved : %s\n", load_config(ssid, sizeof(ssid), pass, sizeof(pass)) ? ssid : "(none)");

    if (!s_started) {
        printf("WiFi  : off\n");
    } else if (s_connected) {
        wifi_ap_record_t ap;
        esp_netif_ip_info_t ip;
        esp_wifi_sta_get_ap_info(&ap);
        esp_netif_get_ip_info(s_netif, &ip);
        printf("WiFi  : connected\n");
        printf("  SSID    : %s\n", (char *)ap.ssid);
        printf("  RSSI    : %d dBm\n", ap.rssi);
        printf("  IP      : " IPSTR "\n", IP2STR(&ip.ip));
        printf("  Gateway : " IPSTR "\n", IP2STR(&ip.gw));
        esp_netif_dns_info_t dns;
        printf("  DNS     :");
        for (int i = ESP_NETIF_DNS_MAIN; i <= ESP_NETIF_DNS_FALLBACK; i++) {
            if (esp_netif_get_dns_info(s_netif, i, &dns) == ESP_OK) {
                printf(" " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
            }
        }
        printf("\n");
    } else if (s_want_connected) {
        printf("WiFi  : connecting\n");
    } else if (s_last_reason) {
        printf("WiFi  : connect failed (reason %d)\n", s_last_reason);
    } else {
        printf("WiFi  : on, not connected\n");
    }
    return 0;
}

static int cmd_txpower(int argc, char **argv)
{
    if (argc >= 3) {
        s_tx_power = (int8_t)(atof(argv[2]) * 4);
        if (s_started) esp_wifi_set_max_tx_power(s_tx_power);
    }
    int8_t q = s_tx_power;
    if (s_started) esp_wifi_get_max_tx_power(&q);
    printf("TX power: %.2f dBm\n", q / 4.0);
    return 0;
}

static int cmd_forget(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    printf("[WiFi] saved config erased\n");
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
        return cmd_scan();
    }
    if (argc >= 3 && strcmp(argv[1], "connect") == 0) {
        if (wifi_connect(resolve_ssid(argv[2]), argc >= 4 ? argv[3] : "") != ESP_OK) {
            printf("[WiFi] connect failed to start\n");
            return 1;
        }
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "disconnect") == 0) {
        return cmd_disconnect();
    }
    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        return cmd_status();
    }
    if (argc >= 2 && strcmp(argv[1], "txpower") == 0) {
        return cmd_txpower(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "forget") == 0) {
        return cmd_forget();
    }
    if (argc >= 2 && strcmp(argv[1], "setup") == 0) {
        if (argc >= 3 && strcmp(argv[2], "stop") == 0) {
            wifi_setup_stop();
            return 0;
        }
        return wifi_setup_start() == ESP_OK ? 0 : 1;
    }
    printf("usage: wifi scan | connect <ssid|no> [password] | disconnect | status | txpower [dBm] | forget | setup [stop]\n");
    return 1;
}

void wifi_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "WiFi control\n"
                "  wifi scan                      : scan access points\n"
                "  wifi connect <ssid> [password] : connect (use \"\" for ssid with spaces)\n"
                "  wifi connect <no> [password]   : connect to AP number <no> of last scan\n"
                "  wifi disconnect                : disconnect and turn off WiFi\n"
                "  wifi status                    : show connection status\n"
                "  wifi txpower [dBm]             : show / set max TX power (2 ~ 21)\n"
                "  wifi forget                    : erase saved WiFi config\n"
                "  wifi setup [stop]              : phone setup mode (QR on screen) / stop it",
        .hint = "<scan|connect|disconnect|status|txpower|forget|setup>",
        .func = &cmd_wifi,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
