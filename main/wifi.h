#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

typedef enum {
    WIFI_CONN_OFF,          /* WiFi RF 꺼짐 */
    WIFI_CONN_IDLE,         /* 켜져 있으나 연결 안 됨 */
    WIFI_CONN_CONNECTING,
    WIFI_CONN_CONNECTED,
    WIFI_CONN_FAILED,       /* 재시도 초과 */
} wifi_conn_state_t;

void wifi_register_commands(void);

/* NVS 에 저장된 설정이 있으면 화면 표시 없이 연결 시도 (실패하면 와이파이를 끔), 없으면 아무것도 안 함 */
void wifi_autoconnect(void);
bool wifi_has_saved(void);

esp_err_t wifi_init(void);     /* 초기화만 (RF 는 켜지 않음) */
esp_err_t wifi_start(void);
void wifi_off(void);

/* 연결에 성공(IP 획득)하면 ssid/password 를 NVS 에 저장 */
esp_err_t wifi_connect(const char *ssid, const char *password);

/* 성공 시 AP 개수 (신호 센 순), 실패 시 -1 */
int wifi_scan(wifi_ap_record_t *aps, uint16_t max);

wifi_conn_state_t wifi_get_state(void);
int wifi_get_last_reason(void);

/* 연결되어 있으면 IP 주소 문자열 */
bool wifi_get_ip(char *buf, size_t len);
