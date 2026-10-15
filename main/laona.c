/*
 * Laona (라오나) 의 말하기
 *   지금은 콘솔로 출력하지만, 나중에 터치/자이로/마이크 입력에 반응해
 *   응답할 때도 laona_say() 를 통해 말하도록 한다.
 *
 * 콘솔 명령
 *   hello   : Laona 가 인사
 */
#include <stdarg.h>
#include <stdio.h>
#include "esp_console.h"
#include "laona.h"

void laona_say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf(LAONA_NAME ": ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static int cmd_hello(int argc, char **argv)
{
    laona_say("안녕하세요, 저는 " LAONA_NAME "(" LAONA_NAME_KO ")예요.");
    return 0;
}

void laona_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "hello",
        .help = "Say hello to " LAONA_NAME,
        .func = &cmd_hello,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
