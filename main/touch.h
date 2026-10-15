#pragma once

#include "esp_err.h"

/* 터치 센서 (GPIO4) 감시 태스크 시작 (face_init 이후 호출) */
esp_err_t touch_init(void);
