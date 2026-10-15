#!/usr/bin/env python3
"""
Laona 한글 비트맵 글꼴 생성기

  TTF/OTF 글꼴에서 펌웨어용 비트맵 글꼴 (main/font.bin) 을 만든다.
  기본: Noto Sans CJK KR (SIL Open Font License 1.1, fonts/OFL.txt)

  사용법: python3 tools/make_font.py [글꼴파일] [face index] [출력]

파일 형식 (little-endian)
  header  : "LAOF" | u8 version=1 | u8 cell_w | u8 cell_h | u8 bpp | u16 nranges | u16 0
  ranges  : nranges x { u32 first_codepoint, u32 count, u32 first_index }
  records : 글자마다 { u8 advance(px) | cell_w*cell_h 픽셀, bpp 비트씩, 위 행부터, MSB 먼저 }
"""
import struct, sys
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc'
INDEX = int(sys.argv[2]) if len(sys.argv) > 2 else 1          # 1 = Noto Sans CJK KR
OUT = sys.argv[3] if len(sys.argv) > 3 else 'main/font.bin'

SIZE = 14          # 글자 크기 (px)
CELL_W, CELL_H = 14, 16
BASELINE = 12      # 셀 안에서 글자 기준선 높이
BPP = 2            # 픽셀당 비트 (4단계 회색조)

RANGES = [
    (0x0020, 0x007E),   # 영문, 숫자, 기호
    (0x3131, 0x318E),   # 한글 호환 자모 (ㅋㅋ, ㅠㅠ)
    (0xAC00, 0xD7A3),   # 한글 완성형 11,172자
]
# 대화에 자주 나오는 기호 (한 글자짜리 범위로 추가)
EXTRA = [0x00B7, 0x2026, 0x2018, 0x2019, 0x201C, 0x201D, 0x2190, 0x2192,
         0x2605, 0x2606, 0x2661, 0x2665, 0x266A, 0x266B, 0x301C, 0xFF5E]

font = ImageFont.truetype(FONT, SIZE, index=INDEX)
levels = (1 << BPP) - 1

def render(cp):
    ch = chr(cp)
    img = Image.new('L', (CELL_W, CELL_H), 0)
    adv = CELL_W if 0xAC00 <= cp <= 0xD7A3 else min(CELL_W, max(1, round(font.getlength(ch))))
    x = (adv - font.getlength(ch)) / 2 if adv < CELL_W or cp >= 0xAC00 else 0
    ImageDraw.Draw(img).text((x, BASELINE), ch, font=font, fill=255, anchor='ls')
    bits, byte, n, out = 0, 0, 0, bytearray([adv])
    for v in img.getdata():
        byte = (byte << BPP) | round(v / 255 * levels)
        n += BPP
        if n == 8:
            out.append(byte)
            byte, n = 0, 0
    if n:
        out.append(byte << (8 - n))
    return bytes(out)

ranges = [(a, b - a + 1) for a, b in RANGES] + [(cp, 1) for cp in EXTRA]
data, table, index = bytearray(), bytearray(), 0
for first, count in ranges:
    table += struct.pack('<III', first, count, index)
    for cp in range(first, first + count):
        data += render(cp)
    index += count

with open(OUT, 'wb') as f:
    f.write(b'LAOF' + struct.pack('<BBBBHH', 1, CELL_W, CELL_H, BPP, len(ranges), 0))
    f.write(table)
    f.write(data)
print(f'{OUT}: {index} glyphs, {12 + len(table) + len(data)} bytes')
