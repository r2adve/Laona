#!/bin/bash
# ESP32-C3 펌웨어 라이팅 (make 로 빌드된 결과물을 다운로드)
#
# 사용법: ./fusing.sh
# 포트/속도 변경: PORT=/dev/ttyUSB0 BAUD=460800 ./fusing.sh
# ※ minicom 이 포트를 열고 있으면 실패하므로 먼저 종료할 것

PORT=${PORT:-/dev/ttyACM0}
BAUD=${BAUD:-460800}

cd "$(dirname "$(readlink -f "$0")")/build" 2>/dev/null && [ -f flash_args ] || {
    echo "빌드 결과물이 없습니다. 먼저 make 를 실행하세요." >&2
    exit 1
}

if ! command -v idf.py >/dev/null 2>&1; then
    source ~/esp/esp-idf/export.sh >/dev/null 2>&1 || {
        echo "ESP-IDF 환경 활성화 실패: ~/esp/esp-idf/export.sh 확인" >&2
        exit 1
    }
fi

python -m esptool --chip esp32c3 -p "$PORT" -b "$BAUD" \
    --before default_reset --after hard_reset write_flash @flash_args
