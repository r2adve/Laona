#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* 가로 방향 */
#define LCD_WIDTH   160
#define LCD_HEIGHT  128

/* 8비트 R, G, B 를 RGB565 로 변환 */
#define LCD_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* SPI 는 상위 바이트부터 보내므로 lcd_draw 에 넘기는 픽셀은 바이트 순서를 바꿔 둔다 */
#define LCD_SWAP(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

esp_err_t lcd_init(void);

/* 사각형 영역을 한 색으로 채움 */
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_fill(uint16_t color);

/* w x h 픽셀 (LCD_SWAP 된 RGB565, DMA 가능 메모리) 을 (x, y) 에 출력 */
void lcd_draw(int x, int y, int w, int h, const uint16_t *pixels);

/* 화면 180도 회전 */
void lcd_set_flip(bool flip);

void lcd_backlight(bool on);

/* 화면 끄기 (백라이트 끄고 LCD 절전) / 켜기 */
void lcd_sleep(bool sleep);

/* 색 이름 또는 RRGGBB 를 RGB565 로 */
bool lcd_parse_color(const char *arg, uint16_t *color);

void lcd_register_commands(void);
