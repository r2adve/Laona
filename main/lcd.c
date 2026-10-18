/*
 * 1.8" TFT 128x160 (ST7735S) SPI 드라이버 - 가로 방향 (160x128) 으로 사용
 *
 *   GPIO10 -- RESET   GPIO20 -- SDA (MOSI)
 *   GPIO9  -- DC      GPIO21 -- SCK (SCLK)
 *   GPIO8  -- CS      GPIO7  -- BL (백라이트, High = 켜짐)
 *
 * 콘솔 명령
 *   lcdout <color>   : 얼굴 표시를 멈추고 화면 전체를 색으로 채움
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "lcd.h"
#include "face.h"

#define PIN_RST   GPIO_NUM_10
#define PIN_DC    GPIO_NUM_9
#define PIN_CS    GPIO_NUM_8
#define PIN_BL    GPIO_NUM_7
#define PIN_MOSI  GPIO_NUM_20
#define PIN_SCLK  GPIO_NUM_21

#define LCD_HOST      SPI2_HOST
#define LCD_SPI_HZ    (20 * 1000 * 1000)

/* 패널에 따라 표시 영역이 컨트롤러 메모리(132x162) 안에서 밀려 있음 (green tab: 2, 1) */
#define LCD_X_OFS  0
#define LCD_Y_OFS  0

/* MADCTL: MX|MV = 가로, MY|MV = 가로 180도 회전. RGB 순서 (BGR 비트 0x08 끄기) */
#define MADCTL_LANDSCAPE      0x60
#define MADCTL_LANDSCAPE_180  0xA0

/* ST7735 명령 */
#define ST_SWRESET  0x01
#define ST_SLPIN    0x10
#define ST_SLPOUT   0x11
#define ST_NORON    0x13
#define ST_INVOFF   0x20
#define ST_DISPOFF  0x28
#define ST_DISPON   0x29
#define ST_CASET    0x2A
#define ST_RASET    0x2B
#define ST_RAMWR    0x2C
#define ST_MADCTL   0x36
#define ST_COLMOD   0x3A

#define CHUNK_LINES 16   /* 한 번에 보내는 줄 수 (160 * 16 * 2 = 5KB) */
#define CHUNK_BYTES (LCD_WIDTH * CHUNK_LINES * 2)

static spi_device_handle_t s_spi;
static uint16_t *s_buf;
static SemaphoreHandle_t s_lock;   /* 얼굴 태스크와 콘솔 태스크가 함께 사용 */

static void lcd_send(bool is_data, const void *data, size_t len)
{
    if (len == 0) {
        return;
    }
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(PIN_DC, is_data);
    /* 큰 데이터는 인터럽트 방식으로 보내 전송 중 CPU 를 다른 태스크에 양보 */
    if (len > 32) {
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
    } else {
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    }
}

static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    lcd_send(false, &cmd, 1);
    lcd_send(true, data, len);
}

static void lcd_set_window(int x, int y, int w, int h)
{
    int x0 = x + LCD_X_OFS, x1 = x0 + w - 1;
    int y0 = y + LCD_Y_OFS, y1 = y0 + h - 1;
    uint8_t col[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd(ST_CASET, col, sizeof(col));
    lcd_cmd(ST_RASET, row, sizeof(row));
    lcd_cmd(ST_RAMWR, NULL, 0);
}

static void fill_locked(int x, int y, int w, int h, uint16_t color);

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH) { w = LCD_WIDTH - x; }
    if (y + h > LCD_HEIGHT) { h = LCD_HEIGHT - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fill_locked(x, y, w, h, color);
    xSemaphoreGive(s_lock);
}

/* s_lock 을 잡은 상태에서 호출 */
static void fill_locked(int x, int y, int w, int h, uint16_t color)
{
    uint16_t be = LCD_SWAP(color);
    int max_px = CHUNK_BYTES / 2;
    for (int i = 0; i < max_px; i++) {
        s_buf[i] = be;
    }
    lcd_set_window(x, y, w, h);
    for (int remain = w * h; remain > 0; remain -= max_px) {
        int n = remain < max_px ? remain : max_px;
        lcd_send(true, s_buf, n * 2);
    }
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void lcd_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    lcd_set_window(x, y, w, h);
    const uint8_t *p = (const uint8_t *)pixels;
    for (int remain = w * h * 2; remain > 0; remain -= CHUNK_BYTES) {
        int n = remain < CHUNK_BYTES ? remain : CHUNK_BYTES;
        lcd_send(true, p, n);
        p += n;
    }
    xSemaphoreGive(s_lock);
}

void lcd_set_flip(bool flip)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    lcd_cmd(ST_MADCTL, (uint8_t[]){ flip ? MADCTL_LANDSCAPE_180 : MADCTL_LANDSCAPE }, 1);
    xSemaphoreGive(s_lock);
}

void lcd_sleep(bool sleep)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* 이 패널은 아무것도 그리지 않은 상태가 흰색이라, 그 순간이 보이지 않게
     * 끌 때는 백라이트를 먼저 끄고, 켤 때는 검게 채운 뒤 마지막에 백라이트를 켠다 */
    if (sleep) {
        lcd_backlight(false);
        vTaskDelay(pdMS_TO_TICKS(30));
        lcd_cmd(ST_DISPOFF, NULL, 0);
        lcd_cmd(ST_SLPIN, NULL, 0);
    } else {
        lcd_cmd(ST_SLPOUT, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));   /* 깨어난 뒤 명령을 받기까지 필요한 시간 */
        fill_locked(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000);
        lcd_cmd(ST_DISPON, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(50));    /* 패널이 검은 화면으로 몇 번 갱신될 때까지 */
        lcd_backlight(true);
    }
    xSemaphoreGive(s_lock);
}

void lcd_backlight(bool on)
{
    gpio_set_level(PIN_BL, on);
}

esp_err_t lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = BIT64(PIN_RST) | BIT64(PIN_DC) | BIT64(PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_BL, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(LCD_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        return err;
    }
    s_buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);
    s_lock = xSemaphoreCreateMutex();
    if (s_buf == NULL || s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 하드웨어 리셋 */
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(ST_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    lcd_cmd(ST_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(ST_COLMOD, (uint8_t[]){ 0x05 }, 1);   /* 16비트 RGB565 */
    lcd_cmd(ST_MADCTL, (uint8_t[]){ MADCTL_LANDSCAPE }, 1);
    lcd_cmd(ST_INVOFF, NULL, 0);
    lcd_cmd(ST_NORON, NULL, 0);

    lcd_fill(0x0000);
    lcd_cmd(ST_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    lcd_backlight(true);
    return ESP_OK;
}

static const struct {
    const char *name;
    uint16_t color;
} s_colors[] = {
    { "black",   LCD_RGB(0, 0, 0) },
    { "white",   LCD_RGB(255, 255, 255) },
    { "red",     LCD_RGB(255, 0, 0) },
    { "green",   LCD_RGB(0, 255, 0) },
    { "blue",    LCD_RGB(0, 0, 255) },
    { "yellow",  LCD_RGB(255, 255, 0) },
    { "cyan",    LCD_RGB(0, 255, 255) },
    { "magenta", LCD_RGB(255, 0, 255) },
    { "orange",  LCD_RGB(255, 165, 0) },
    { "gray",    LCD_RGB(128, 128, 128) },
};

/* 색 이름 또는 6자리 16진수 RRGGBB (앞의 # / 0x 는 생략 가능) */
bool lcd_parse_color(const char *arg, uint16_t *color)
{
    for (int i = 0; i < sizeof(s_colors) / sizeof(s_colors[0]); i++) {
        if (strcasecmp(arg, s_colors[i].name) == 0) {
            *color = s_colors[i].color;
            return true;
        }
    }
    if (arg[0] == '#') {
        arg++;
    } else if (strncasecmp(arg, "0x", 2) == 0) {
        arg += 2;
    }
    char *end;
    unsigned long rgb = strtoul(arg, &end, 16);
    if (strlen(arg) != 6 || *end != '\0') {
        return false;
    }
    *color = LCD_RGB(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

static int cmd_lcdout(int argc, char **argv)
{
    uint16_t color;
    if (argc != 2 || !lcd_parse_color(argv[1], &color)) {
        printf("usage: lcdout <color>\n  color: ");
        for (int i = 0; i < sizeof(s_colors) / sizeof(s_colors[0]); i++) {
            printf("%s ", s_colors[i].name);
        }
        printf("| RRGGBB (ex: lcdout ff8000)\n");
        return 1;
    }
    face_pause();
    lcd_fill(color);
    return 0;
}

void lcd_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "lcdout",
        .help = "Stop face and fill LCD with a color (\"laona\" to resume)\n"
                "  lcdout <name>   : black white red green blue yellow cyan magenta orange gray\n"
                "  lcdout RRGGBB   : hex color (ex: lcdout ff8000)",
        .hint = "<color>",
        .func = &cmd_lcdout,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
