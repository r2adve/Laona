#!/usr/bin/env python3
"""preview_seq 가 내보낸 프레임으로 화면 끄기/켜기 연출 GIF 를 만든다."""
import subprocess, sys
from PIL import Image, ImageDraw

W, H, SCALE, LABEL = 160, 128, 3, 28
exe, out = sys.argv[1], sys.argv[2]

raw = subprocess.run([exe], capture_output=True, check=True).stdout
frames, pos = [], 0
while pos < len(raw):
    nl = raw.index(b'\n', pos)
    name = raw[pos:nl].decode()
    data = raw[nl + 1:nl + 1 + W * H * 3]
    pos = nl + 1 + W * H * 3
    img = Image.frombytes('RGB', (W, H), data).resize((W * SCALE, H * SCALE), Image.NEAREST)
    canvas = Image.new('RGB', (W * SCALE, H * SCALE + LABEL), (50, 50, 60))
    canvas.paste(img, (0, 0))
    ImageDraw.Draw(canvas).text((10, H * SCALE + 8), name, fill=(230, 230, 230))
    frames.append(canvas)

# 여러 장면이 담기도록 띄엄띄엄 고른 프레임으로 팔레트를 만듦
step = max(1, len(frames) // 12)
sample = Image.new('RGB', (frames[0].width, frames[0].height * 12))
for i in range(12):
    sample.paste(frames[min(i * step, len(frames) - 1)], (0, i * frames[0].height))
pal = sample.quantize(64)
gif = [f.quantize(palette=pal, dither=0) for f in frames]
gif[0].save(out, save_all=True, append_images=gif[1:], duration=40, loop=0, optimize=True)

# 연출 단계별 장면 모음
picks = [frames[i] for i in (30, 55, 75, 90, 105, 115, 145, 160, 172, 200)]
sheet = Image.new('RGB', (frames[0].width * 5, frames[0].height * 2))
for i, f in enumerate(picks):
    sheet.paste(f, ((i % 5) * f.width, (i // 5) * f.height))
sheet.save(out.replace('.gif', '_sheet.png'))
print(f'{out} ({len(frames)} frames)')
