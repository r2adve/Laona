#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "lcd.h"
#include "face.h"
#include "laona.h"
#include "touch.h"
#include "llm.h"
#include "config.h"

static int cmd_ver(int argc, char **argv)
{
    printf("%s\n", esp_app_get_description()->version);
    return 0;
}

/* WiFi 설정과 RF 보정값 저장에 NVS 를 사용 */
static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
/*
 * USB 콘솔 입력 루프.
 * ESP-IDF 기본 REPL 은 USB 가 PC 에 연결되지 않으면 (충전기, 보조배터리 등) 읽기가 바로
 * 실패해서 쉬지 않고 계속 돌며 CPU 를 독차지한다. 그러면 우선순위가 낮은 얼굴 태스크가
 * 멈춘다. 그래서 연결되어 있지 않을 때는 기다리도록 직접 루프를 돌린다.
 */
static void console_task(void *arg)
{
    const char *prompt = arg;
    bool was_connected = true;

    setvbuf(stdin, NULL, _IONBF, 0);
    linenoiseSetMaxLineLen(256);
    for (;;) {
        if (!usb_serial_jtag_is_connected()) {
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!was_connected) {
            /* 새로 연결된 터미널이 줄 편집 (ESC 시퀀스) 을 지원하는지 다시 확인 */
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(500));
            linenoiseSetDumbMode(linenoiseProbe() != 0);
        }

        char *line = linenoise(prompt);
        if (line == NULL) {
            continue;   /* 빈 줄 */
        }
        linenoiseHistoryAdd(line);
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        linenoiseFree(line);
    }
}
#endif

void app_main(void)
{
    nvs_init();

    esp_err_t err = lcd_init();
    if (err != ESP_OK) {
        printf("LCD init failed (%s)\n", esp_err_to_name(err));
    } else if ((err = face_init()) != ESP_OK) {
        printf("Face init failed (%s)\n", esp_err_to_name(err));
    } else if ((err = touch_init()) != ESP_OK) {
        printf("Touch init failed (%s)\n", esp_err_to_name(err));
    }
    if ((err = llm_init()) != ESP_OK) {
        printf("LLM init failed (%s)\n", esp_err_to_name(err));
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = LAONA_HOSTNAME ">";
    repl_config.task_stack_size = 8192;

    /* 콘솔 장치는 sdkconfig 의 CONFIG_ESP_CONSOLE_* 설정을 따른다 */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    /* 드라이버/줄 편집 설정만 쓰고 입력 루프는 console_task 가 돌린다
     * (기본 REPL 태스크는 시작 신호를 기다린 채로 남으므로 스택을 작게) */
    repl_config.task_stack_size = 2048;
    esp_console_dev_usb_serial_jtag_config_t dev_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &repl));
#else
    esp_console_dev_uart_config_t dev_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev_config, &repl_config, &repl));
#endif

    ESP_ERROR_CHECK(esp_console_register_help_command());
    const esp_console_cmd_t ver_cmd = {
        .command = "ver",
        .help = "Print firmware version",
        .func = &cmd_ver,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ver_cmd));
    wifi_register_commands();
    lcd_register_commands();
    face_register_commands();
    laona_register_commands();
    llm_register_commands();
    config_register_commands();

    laona_say("안녕하세요, " LAONA_NAME "(" LAONA_NAME_KO ")예요. (%s)", esp_app_get_description()->version);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    xTaskCreate(console_task, "console", 8192, (void *)(LAONA_HOSTNAME ">"), 2, NULL);
#else
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
#endif

    /* 저장된 WiFi 설정이 있으면 자동 연결 */
    wifi_autoconnect();
}
