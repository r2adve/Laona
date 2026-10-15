/*
 * WiFi STA - 콘솔 명령으로 필요할 때만 켜고 연결
 *   wifi scan
 *   wifi connect <ssid | scan 번호> [password]
 *   wifi disconnect
 *   wifi status
 *   wifi forget
 *
 * 연결에 성공한 설정은 NVS 에 저장되어 다음 부팅 때 자동 연결된다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "wifi.h"
#include "laona.h"
#include "face.h"
#include "web.h"

#define MAX_RETRY      5
#define MAX_SCAN_APS   20
#define NVS_NAMESPACE  "wifi"

static bool s_started;
static bool s_want_connected;   /* 사용자가 연결을 요청한 상태 (끊기면 재시도) */
static bool s_connected;
static int s_retry;
static int s_last_reason;
static esp_netif_t *s_netif;
static wifi_ap_record_t s_aps[MAX_SCAN_APS];   /* 마지막 scan 결과 (번호로 연결할 때 사용) */
static uint16_t s_num_aps;

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
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_connected = true;
        s_retry = 0;
        save_config();
        printf("[WiFi] connected, ip " IPSTR "\n", IP2STR(&e->ip_info.ip));
        web_start();
        /* 부팅 후 처음 연결되면 휴대폰 접속 주소 QR 을 잠깐 보여줌 */
        static bool s_shown;
        if (!s_shown) {
            s_shown = true;
            web_show_address();
        }
    }
}

/* 처음 호출될 때 초기화하고 RF 를 켠다 */
static esp_err_t wifi_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
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
    esp_err_t err = esp_wifi_start();
    s_started = (err == ESP_OK);
    return err;
}

static esp_err_t wifi_connect(const char *ssid, const char *password)
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

void wifi_autoconnect(void)
{
    char ssid[33], pass[65];
    if (load_config(ssid, sizeof(ssid), pass, sizeof(pass))) {
        wifi_connect(ssid, pass);
    }
}

static int cmd_scan(void)
{
    printf("[WiFi] scanning...\n");
    uint16_t num = MAX_SCAN_APS;
    if (wifi_start() != ESP_OK || esp_wifi_scan_start(NULL, true) != ESP_OK) {
        printf("[WiFi] scan failed\n");
        return 1;
    }
    esp_wifi_scan_get_ap_records(&num, s_aps);
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
    s_want_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_started = false;
    s_connected = false;
    printf("[WiFi] off\n");
    return 0;
}

static int cmd_status(void)
{
    char ssid[33], pass[65];
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
    } else if (s_want_connected) {
        printf("WiFi  : connecting\n");
    } else if (s_last_reason) {
        printf("WiFi  : connect failed (reason %d)\n", s_last_reason);
    } else {
        printf("WiFi  : on, not connected\n");
    }
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
    if (argc >= 2 && strcmp(argv[1], "forget") == 0) {
        return cmd_forget();
    }
    printf("usage: wifi scan | connect <ssid|no> [password] | disconnect | status | forget\n");
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
                "  wifi forget                    : erase saved WiFi config",
        .hint = "<scan|connect|disconnect|status|forget>",
        .func = &cmd_wifi,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
