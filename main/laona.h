#pragma once

/* 로봇 이름 - 스스로를 부를 때는 항상 이 값을 사용 */
#define LAONA_NAME      "Laona"
#define LAONA_NAME_KO   "라오나"
#define LAONA_HOSTNAME  "laona"   /* WiFi 공유기에 보이는 장치 이름 */

/* Laona 가 말하는 문장 출력: "Laona: ..." */
void laona_say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void laona_register_commands(void);
