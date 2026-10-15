/*
 * Laona 의 얼굴 - 두 가지 스타일 중 하나를 25fps 로 그려 LCD 에 출력
 *   kao  : 이모티콘 얼굴 (kao.c)
 *   eyes : 흰 둥근 눈 (eyes.c, 처음 만든 형태)
 *
 * 콘솔 명령
 *   laona                  : 얼굴 표시 (lcdout 으로 멈춘 경우 다시 시작) / 상태
 *   laona style kao|eyes   : 얼굴 스타일
 *   laona <표정>           : normal happy surprised dizzy cry sad wink sleepy angry stare tongue meh glance
 *                            (스타일에 없는 표정은 가장 비슷한 표정으로)
 *   laona look <방향>      : left right up down center
 *   laona blink
 *   laona auto on|off      : 표정을 저절로 바꾸기
 *   laona invert [on|off]  : 검은 바탕 + 흰 선 (스타일을 바꿔도 유지). 인자 없으면 전환
 *   laona color <color>    : eyes 스타일 눈 색 (lcdout 과 같은 색 이름 또는 RRGGBB)
 *   laona flip             : 화면 180도 회전
 *   laona say <표정> <말>  : 표정을 짓고 화면 아래에 말을 보여줌 (대답 표시 시험용)
 *   laona qr               : 휴대폰 접속 주소 QR 코드 (터치 세 번과 같음)
 *   laona screen [on|off]  : 화면 끄기/켜기. 인자 없으면 전환
 *                            끌 때는 하품하고 잠든 뒤 위아래에서 닫히고, 켤 때는 중앙에서 열린다
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lcd.h"
#include "face.h"
#include "kao.h"
#include "eyes.h"
#include "seq.h"
#include "text.h"
#include "web.h"
#include "qrcode.h"
#include "laona.h"

#define FRAME_MS  40      /* 25 fps */
#define TEXT_LIFT 16      /* 아래 글자가 나올 때 얼굴을 올리는 높이 (px) */
#define C_INK     LCD_RGB(40, 36, 48)   /* kao.c 의 선 색과 같음 */

#define QR_MAX    37      /* QR 버전 5 까지 (주소는 버전 2 면 충분) */
#define QR_QUIET  2       /* QR 둘레 여백 (모듈 수) */

/* tools/make_font.py 로 만든 한글 글꼴 (main/CMakeLists.txt 의 EMBED_FILES) */
extern const uint8_t font_start[] asm("_binary_font_bin_start");
extern const uint8_t font_end[] asm("_binary_font_bin_end");

typedef enum {
    STYLE_KAO,
    STYLE_EYES,
} face_style_t;

static const char *s_style_names[] = { "kao", "eyes" };

/* 한쪽 스타일에만 있는 표정을 다른 스타일의 비슷한 표정으로 */
static const struct {
    const char *from, *to;
} s_expr_map[] = {
    { "sad", "cry" }, { "sleep", "sleepy" },            /* -> kao */
    { "dizzy", "surprised" }, { "cry", "sad" },         /* -> eyes */
    { "wink", "happy" }, { "stare", "surprised" }, { "tongue", "happy" },
    { "meh", "sleepy" }, { "glance", "normal" },
};

static uint16_t *s_fb;
static SemaphoreHandle_t s_lock;
static bool s_paused;
static bool s_screen_off;             /* 화면 꺼짐 (그리기도 멈춤) */
static bool s_flip;
static bool s_auto = true;
static bool s_saved_auto;             /* 끄기 연출 중 자동 표정을 멈추기 전 값 */
static bool s_invert;                 /* 검은 바탕 (스타일을 바꿔도 유지) */
static face_style_t s_style = STYLE_KAO;
static int s_render_us, s_draw_us;   /* 최근 프레임의 그리기 / LCD 전송 시간 */
static bool s_talking;                /* 아래에 글자를 보여주는 중 */
static uint8_t s_qr[QR_MAX * QR_MAX];  /* QR 모듈 (1 = 검정) */
static int s_qr_size, s_qr_left_ms;   /* QR 한 변 모듈 수, 남은 표시 시간 (0 이면 안 보임) */
static char s_qr_caption[2][40];
static float s_lift;                  /* 얼굴을 올린 정도 (px) */

static bool set_expr(const char *name);
static void set_auto(bool on);

/* 흰 바탕에 QR 과 아래 두 줄 안내. 반전 모드여도 스캔이 잘 되게 항상 흰 바탕 */
static void draw_qr(void)
{
    for (int i = 0; i < KAO_W * KAO_H; i++) s_fb[i] = 0xFFFF;
    int area = KAO_H - TEXT_AREA_H, n = s_qr_size + QR_QUIET * 2;
    int scale = area / n;
    if (scale < 1) scale = 1;
    int x0 = (KAO_W - s_qr_size * scale) / 2, y0 = (area - s_qr_size * scale) / 2;
    for (int y = 0; y < s_qr_size; y++) {
        for (int x = 0; x < s_qr_size; x++) {
            if (!s_qr[y * s_qr_size + x]) continue;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    s_fb[(y0 + y * scale + dy) * KAO_W + x0 + x * scale + dx] = 0x0000;
                }
            }
        }
    }
    int ty = area + (TEXT_AREA_H - 32) / 2;
    text_draw_line(s_fb, KAO_W, KAO_H, ty, s_qr_caption[0], C_INK, 0xFFFF);
    text_draw_line(s_fb, KAO_W, KAO_H, ty + 16, s_qr_caption[1], C_INK, 0xFFFF);
}

static void face_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_paused && !s_screen_off) {
            int64_t t0 = esp_timer_get_time();
            /* 화면 끄기/켜기 연출 */
            seq_out_t sq = { 0 };
            bool in_seq = seq_active(), turning_off = seq_turning_off();
            if (in_seq) {
                seq_step(FRAME_MS, &sq);
                if (sq.expr) set_expr(sq.expr);
            }
            /* 아래 글자: 보여주는 동안에는 자동 표정을 멈추고 얼굴을 위로 올림 */
            text_update(FRAME_MS);
            if (text_active() != s_talking) {
                s_talking = text_active();
                kao_set_auto(s_talking ? false : s_auto);
                eyes_set_auto(s_talking ? false : s_auto);
            }
            s_lift += ((s_talking ? TEXT_LIFT : 0) - s_lift) * 0.25f;
            kao_set_offset(-s_lift);
            eyes_set_offset(-s_lift);
            /* 두 스타일 모두 시간을 진행시켜 바꿨을 때 자연스럽게 이어지게 함 */
            kao_update(FRAME_MS);
            eyes_update(FRAME_MS);
            if (s_qr_left_ms > 0) {
                s_qr_left_ms -= FRAME_MS;
                draw_qr();
            } else {
                if (s_style == STYLE_KAO) {
                    kao_render();
                } else {
                    eyes_render();
                }
                text_render(s_fb, KAO_W, KAO_H, s_invert ? 0xFFFF : C_INK, s_invert ? 0x0000 : 0xFFFF);
            }
            if (in_seq) {
                seq_draw_shutter(s_fb, KAO_W, KAO_H, sq.shutter);
            }
            /* SPI 는 상위 바이트부터 보내므로 바이트 순서를 바꿈 */
            for (int i = 0; i < KAO_W * KAO_H; i++) {
                s_fb[i] = LCD_SWAP(s_fb[i]);
            }
            int64_t t1 = esp_timer_get_time();
            lcd_draw(0, 0, KAO_W, KAO_H, s_fb);
            s_render_us = t1 - t0;
            s_draw_us = esp_timer_get_time() - t1;
            if (sq.done) {
                if (turning_off) {
                    s_screen_off = true;     /* 다 닫혔으면 LCD 를 끔 */
                    lcd_sleep(true);
                } else {
                    set_auto(s_saved_auto);  /* 다 켜졌으면 자동 표정 원래대로 */
                }
            }
        }
        xSemaphoreGive(s_lock);
        /* 그리기가 늦어져도 다른 태스크가 돌 수 있게 최소 1틱은 쉼 */
        if (xTaskGetTickCount() - wake >= pdMS_TO_TICKS(FRAME_MS)) {
            wake = xTaskGetTickCount();
            vTaskDelay(1);
        } else {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
        }
    }
}

esp_err_t face_init(void)
{
    s_fb = heap_caps_malloc(KAO_W * KAO_H * 2, MALLOC_CAP_DMA);
    uint8_t *cov = heap_caps_malloc(KAO_W * KAO_H, MALLOC_CAP_8BIT);
    s_lock = xSemaphoreCreateMutex();
    if (s_fb == NULL || cov == NULL || s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!text_init(font_start, font_end - font_start)) {
        printf("Font: invalid font.bin\n");
    }
    kao_init(s_fb, cov, esp_random());
    eyes_init(s_fb, esp_random());
    /* 부팅 기본은 검은 바탕 (eyes 는 원래 검은 바탕이라 반전하지 않음) */
    s_invert = true;
    kao_set_invert(true);
    eyes_set_invert(false);
    /* 콘솔 (우선순위 2) 보다 낮게 두어 명령 입력이 항상 반응하게 함 */
    if (xTaskCreate(face_task, "face", 4096, NULL, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void face_pause(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_paused = true;
    xSemaphoreGive(s_lock);
}

/* 아래 함수들은 s_lock 을 잡은 상태에서 호출 */
static void set_invert(bool on)
{
    /* 바탕색 기준으로 맞춤: kao 는 반전하면 검은 바탕, eyes 는 원래 검은 바탕 */
    s_invert = on;
    kao_set_invert(on);
    eyes_set_invert(!on);
}

static void set_auto(bool on)
{
    s_auto = on;
    kao_set_auto(on);
    eyes_set_auto(on);
}

/* 연출 중에는 무시. 끄기는 연출이 끝난 뒤 face_task 에서 LCD 를 끈다 */
static void set_screen(bool on)
{
    if (seq_active()) return;
    if (!on && !s_screen_off) {
        s_saved_auto = s_auto;
        set_auto(false);
        seq_start_off();
    } else if (on && s_screen_off) {
        lcd_sleep(false);   /* 화면에는 다 닫힌 (검은) 마지막 프레임이 남아 있음 */
        s_screen_off = false;
        seq_start_on();
    }
}

static void qr_copy(esp_qrcode_handle_t qr, void *arg)
{
    int n = esp_qrcode_get_size(qr);
    s_qr_size = n <= QR_MAX ? n : 0;
    for (int y = 0; y < s_qr_size; y++) {
        for (int x = 0; x < s_qr_size; x++) {
            s_qr[y * n + x] = esp_qrcode_get_module(qr, x, y);
        }
    }
}

void face_show_qr(const char *text, const char *line1, const char *line2, int ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_paused = false;
    if (s_screen_off) set_screen(true);
    esp_qrcode_config_t cfg = {
        .display_func_with_cb = qr_copy,
        .max_qrcode_version = 5,
        .qrcode_ecc_level = ESP_QRCODE_ECC_MED,
        .user_data = s_qr,   /* NULL 이 아니어야 display_func_with_cb 를 부름 */
    };
    s_qr_size = 0;
    if (esp_qrcode_generate(&cfg, text) == ESP_OK && s_qr_size > 0) {
        snprintf(s_qr_caption[0], sizeof(s_qr_caption[0]), "%s", line1);
        snprintf(s_qr_caption[1], sizeof(s_qr_caption[1]), "%s", line2);
        s_qr_left_ms = ms;
    }
    xSemaphoreGive(s_lock);
}

void face_hide_qr(void)
{
    s_qr_left_ms = 0;
}

bool face_qr_shown(void)
{
    return s_qr_left_ms > 0;
}

void face_thinking(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_paused = false;
    s_qr_left_ms = 0;
    if (s_screen_off) set_screen(true);
    text_thinking(on);
    if (on) set_expr("glance");   /* 곁눈질하며 생각 */
    xSemaphoreGive(s_lock);
}

void face_say(const char *emotion, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_paused = false;
    s_qr_left_ms = 0;
    if (s_screen_off) set_screen(true);
    if (emotion) set_expr(emotion);
    text_show(text);
    xSemaphoreGive(s_lock);
}

/* 꺼졌거나 꺼지는 중이면 false */
static bool screen_on(void)
{
    return !s_screen_off && !seq_turning_off();
}

const char *face_next_style(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_style = s_style == STYLE_KAO ? STYLE_EYES : STYLE_KAO;
    const char *name = s_style_names[s_style];
    xSemaphoreGive(s_lock);
    return name;
}

bool face_toggle_invert(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_invert(!s_invert);
    bool on = s_invert;
    xSemaphoreGive(s_lock);
    return on;
}

bool face_toggle_screen(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_screen(!screen_on());
    bool on = screen_on();
    xSemaphoreGive(s_lock);
    return on;
}

bool face_screen_on(void)
{
    return screen_on();
}

static const char *map_expr(const char *name)
{
    for (int i = 0; i < sizeof(s_expr_map) / sizeof(s_expr_map[0]); i++) {
        if (strcmp(name, s_expr_map[i].from) == 0) return s_expr_map[i].to;
    }
    return name;
}

/* 두 스타일 모두에 표정을 적용. 어느 쪽에도 없는 이름이면 false */
static bool set_expr(const char *name)
{
    int k = kao_find(name), e = eyes_find(name);
    if (k < 0 && e < 0) return false;
    kao_set_expr(k >= 0 ? k : kao_find(map_expr(name)));
    eyes_set_expr(e >= 0 ? e : eyes_find(map_expr(name)));
    return true;
}

static int cmd_look(const char *dir)
{
    static const struct { const char *name; float x, y; } dirs[] = {
        { "center", 0, 0 }, { "left", -1, 0 }, { "right", 1, 0 },
        { "up", 0, -0.6f }, { "down", 0, 0.6f },
    };
    for (int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (strcmp(dir, dirs[i].name) == 0) {
            kao_look(dirs[i].x, dirs[i].y);
            eyes_look(dirs[i].x, dirs[i].y);
            return 0;
        }
    }
    printf("usage: laona look <left|right|up|down|center>\n");
    return 1;
}

static void print_status(void)
{
    const char *emo = s_style == STYLE_KAO ? kao_name(kao_current()) : eyes_name(eyes_current());
    printf("Face   : %s, style %s\n",
           s_screen_off ? "screen off" : seq_turning_off() ? "turning off" : (s_paused ? "paused" : "running"),
           s_style_names[s_style]);
    printf("Emotion: %s (auto %s, invert %s)\n", emo, s_auto ? "on" : "off", s_invert ? "on" : "off");
    printf("Frame  : render %d.%d ms + lcd %d.%d ms (budget %d ms)\n",
           s_render_us / 1000, s_render_us / 100 % 10, s_draw_us / 1000, s_draw_us / 100 % 10, FRAME_MS);
}

static int cmd_laona(int argc, char **argv)
{
    int ret = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_paused = false;   /* lcdout 으로 멈춰 있었다면 다시 시작 */

    if (argc == 1) {
        print_status();
    } else if (set_expr(argv[1])) {
        s_auto = false;   /* 직접 고른 표정을 유지 */
        kao_set_auto(false);
        eyes_set_auto(false);
    } else if (strcmp(argv[1], "style") == 0 && argc >= 3 &&
               (strcmp(argv[2], "kao") == 0 || strcmp(argv[2], "eyes") == 0)) {
        s_style = strcmp(argv[2], "kao") == 0 ? STYLE_KAO : STYLE_EYES;
    } else if (strcmp(argv[1], "look") == 0 && argc >= 3) {
        ret = cmd_look(argv[2]);
    } else if (strcmp(argv[1], "blink") == 0) {
        kao_blink();
        eyes_blink();
    } else if (strcmp(argv[1], "auto") == 0 && argc >= 3) {
        set_auto(strcmp(argv[2], "on") == 0);
        printf("auto %s\n", s_auto ? "on" : "off");
    } else if (strcmp(argv[1], "invert") == 0) {
        set_invert(argc >= 3 ? strcmp(argv[2], "on") == 0 : !s_invert);
        printf("invert %s\n", s_invert ? "on" : "off");
    } else if (strcmp(argv[1], "color") == 0 && argc >= 3) {
        uint16_t color;
        if (lcd_parse_color(argv[2], &color)) {
            eyes_set_color(color);
        } else {
            printf("unknown color: %s\n", argv[2]);
            ret = 1;
        }
    } else if (strcmp(argv[1], "qr") == 0) {
        xSemaphoreGive(s_lock);
        web_show_address();     /* face_show_qr 가 다시 잠금을 잡음 */
        return 0;
    } else if (strcmp(argv[1], "say") == 0 && argc >= 4) {
        char buf[300] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3) strlcat(buf, " ", sizeof(buf));
            strlcat(buf, argv[i], sizeof(buf));
        }
        set_expr(argv[2]);
        text_show(buf);
    } else if (strcmp(argv[1], "screen") == 0) {
        set_screen(argc >= 3 ? strcmp(argv[2], "on") == 0 : !screen_on());
        printf("screen %s\n", screen_on() ? "on" : "off");
    } else if (strcmp(argv[1], "flip") == 0) {
        s_flip = !s_flip;
        lcd_set_flip(s_flip);
    } else {
        printf("usage: laona [<emotion> | style kao|eyes | look <dir> | blink | auto on|off |\n"
               "              invert [on|off] | color <color> | screen [on|off] | flip]\n"
               "  emotion: normal happy surprised dizzy cry sad wink sleepy angry stare tongue meh glance\n");
        ret = 1;
    }

    xSemaphoreGive(s_lock);
    return ret;
}

void face_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "laona",
        .help = LAONA_NAME " face\n"
                "  laona                : show face / status\n"
                "  laona style kao|eyes : emoticon face / big round eyes\n"
                "  laona <emotion>      : normal happy surprised dizzy cry sad wink\n"
                "                         sleepy angry stare tongue meh glance\n"
                "  laona look <dir>     : left right up down center\n"
                "  laona blink          : blink once\n"
                "  laona auto on|off    : change emotion automatically\n"
                "  laona invert [on|off]: black background, kept across styles (toggle)\n"
                "  laona color <color>  : eye color of eyes style (same as lcdout)\n"
                "  laona screen [on|off]: turn screen off/on (toggle without argument)\n"
                "  laona say <emo> <text>: show emotion and text (test reply display)\n"
                "  laona qr             : show web address QR code (same as triple tap)\n"
                "  laona flip           : rotate screen 180 degrees",
        .hint = "[<emotion>|style|look|blink|auto|invert|color|screen|flip]",
        .func = &cmd_laona,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
