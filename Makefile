# ESP32 빌드
#   make        : 펌웨어 컴파일
#   make clean  : 빌드 결과물 삭제

SHELL := /bin/bash
IDF_ENV := command -v idf.py >/dev/null 2>&1 || source ~/esp/esp-idf/export.sh >/dev/null 2>&1

.PHONY: all clean menuconfig

all:
	@$(IDF_ENV); idf.py --ccache build

clean:
	@$(IDF_ENV); idf.py fullclean

menuconfig:
	@$(IDF_ENV); idf.py menuconfig
