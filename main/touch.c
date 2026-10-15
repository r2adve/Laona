/*
 * 터치 센서 (GPIO4, TTP223 같은 디지털 출력 모듈)
 *
 *   한 번 터치        : 얼굴 스타일 바꾸기 (kao <-> eyes)
 *   빠르게 두 번 터치 : 흑백 반전
 *   빠르게 세 번 터치 : 휴대폰 접속 주소 QR 코드
 *   QR 이 떠 있을 때  : 아무 터치나 하면 닫힘
 *   1.5초 이상 누름   : 화면 끄기 / 켜기 (누르고 있는 중 1.5초가 되는 순간 동작)
 *
 *   화면이 꺼져 있을 때는 길게 누르기만 받는다.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "face.h"
#include "touch.h"
#include "web.h"

#define PIN_TOUCH     GPIO_NUM_4
#define TOUCH_ACTIVE  1       /* 터치 시 출력 레벨. TTP223 기본은 High (A 점퍼를 붙이면 Low) */

#define POLL_MS       10
#define DEBOUNCE      3       /* 같은 값이 이만큼 연속으로 읽혀야 바뀐 것으로 봄 (30ms) */
#define LONG_MS       1500    /* 길게 누르기 */
#define MULTI_MS      300     /* 손을 뗀 뒤 이 시간 안에 다시 누르면 연속 터치로 셈 */

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* taps 번 연속 터치가 끝났을 때 */
static void on_taps(int taps)
{
    if (!face_screen_on()) return;
    if (face_qr_shown()) {
        face_hide_qr();
        printf("[Touch] tap -> close QR\n");
    } else if (taps == 1) {
        printf("[Touch] tap -> style %s\n", face_next_style());
    } else if (taps == 2) {
        printf("[Touch] double tap -> invert %s\n", face_toggle_invert() ? "on" : "off");
    } else {
        printf("[Touch] triple tap -> address QR\n");
        web_show_address();
    }
}

static void on_long(void)
{
    printf("[Touch] long press -> screen %s\n", face_toggle_screen() ? "on" : "off");
}

static void touch_task(void *arg)
{
    bool pressed = false, long_fired = false;
    int taps = 0;                   /* 지금까지 연속으로 터치한 횟수 */
    int same = 0;
    int64_t press_t = 0, release_t = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t now = now_ms();

        /* 떨림 제거: DEBOUNCE 번 연속으로 다르게 읽혀야 상태를 바꿈 */
        bool raw = gpio_get_level(PIN_TOUCH) == TOUCH_ACTIVE;
        same = raw != pressed ? same + 1 : 0;
        if (same >= DEBOUNCE) {
            same = 0;
            pressed = raw;
            if (pressed) {
                press_t = now;
                long_fired = false;
            } else if (!long_fired) {
                taps++;                     /* 다음 터치가 오는지 기다림 */
                release_t = now;
            }
        }

        if (pressed && !long_fired && now - press_t >= LONG_MS) {
            long_fired = true;
            taps = 0;
            on_long();
        }
        /* 다음 터치가 시작되지 않고 시간이 지나면 그동안의 횟수로 처리 */
        if (taps > 0 && !pressed && now - release_t > MULTI_MS) {
            on_taps(taps);
            taps = 0;
        }
    }
}

esp_err_t touch_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = BIT64(PIN_TOUCH),
        .mode = GPIO_MODE_INPUT,
        /* 센서가 빠져 있어도 '안 눌림' 으로 읽히게 */
        .pull_down_en = TOUCH_ACTIVE ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .pull_up_en = TOUCH_ACTIVE ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreate(touch_task, "touch", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
