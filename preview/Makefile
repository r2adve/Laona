# 얼굴 미리보기 (PC 에서 main/kao.c 실행)
#   make        : out/kao.gif (움직임), out/kao_sheet.png (표정 모음) 생성
#   PER=4000 make : 표정 하나당 보여주는 시간 (ms)
#   반전 모드 (검은 바탕) 는 out/kao_invert.gif, out/kao_invert_sheet.png
#   화면 끄기/켜기 연출은 out/screen.gif, 대화 표시는 out/chat.gif, 와이파이 아이콘은 out/wifi_icon.png

PER ?= 3000

all:
	@mkdir -p out
	@gcc -O2 -Wall -I../main -o out/preview preview.c ../main/kao.c -lm
	@python3 make_gif.py out/preview out $(PER) 0
	@python3 make_gif.py out/preview out $(PER) 1
	@gcc -O2 -Wall -I../main -o out/preview_seq preview_seq.c ../main/kao.c ../main/eyes.c ../main/seq.c -lm
	@python3 make_seq_gif.py out/preview_seq out/screen.gif
	@gcc -O2 -Wall -I../main -o out/preview_text preview_text.c ../main/kao.c ../main/eyes.c ../main/text.c -lm
	@python3 make_seq_gif.py out/preview_text out/chat.gif
	@gcc -O2 -Wall -I../main -o out/preview_icon preview_icon.c ../main/kao.c ../main/eyes.c ../main/icon.c -lm
	@python3 make_icon_png.py out/preview_icon out/wifi_icon.png

clean:
	rm -rf out
