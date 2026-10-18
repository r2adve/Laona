#pragma once

#include "esp_err.h"

/* 터치 센서 (GPIO2) 와 택트 스위치 (GPIO0) 감시 태스크 시작 (face_init 이후 호출) */
esp_err_t touch_init(void);
