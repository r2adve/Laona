#pragma once

#include <stdbool.h>
#include <stddef.h>

void wifi_register_commands(void);

/* NVS 에 저장된 설정이 있으면 부팅 시 자동 연결 */
void wifi_autoconnect(void);

/* 연결되어 있으면 IP 주소 문자열 */
bool wifi_get_ip(char *buf, size_t len);
