#pragma once
/*
 * 휴대폰으로 와이파이 설정 (USB 콘솔 없이)
 *   라오나가 설정용 와이파이 (Laona-XXXX) 를 켜고 화면에 QR 을 보여준다.
 *   1) 휴대폰 카메라로 QR 을 찍으면 설정용 와이파이에 접속
 *   2) 설정 페이지가 저절로 뜸 (안 뜨면 화면의 두 번째 QR)
 *   3) 집 와이파이를 고르고 비밀번호 입력 -> 연결되면 화면에 대화 페이지 QR
 */
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_SETUP_URL "http://4.3.2.1/"

esp_err_t wifi_setup_start(void);   /* 이미 켜져 있으면 QR 만 다시 보여줌 */
void wifi_setup_stop(void);
bool wifi_setup_running(void);

/* 터치: 와이파이 QR <-> 설정 페이지 주소 QR 을 번갈아 보여줌 */
void wifi_setup_next_qr(void);

const char *wifi_setup_ap_ssid(void);
const char *wifi_setup_ap_pass(void);
