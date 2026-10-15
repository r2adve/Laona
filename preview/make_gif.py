#!/usr/bin/env python3
"""preview 프로그램이 내보낸 프레임을 GIF (움직임) 와 PNG (표정 모음) 로 만든다."""
import subprocess, sys
from PIL import Image, ImageDraw

W, H, SCALE, LABEL = 160, 128, 3, 28
exe, out_dir, per, inv = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
suffix = '_invert' if inv == '1' else ''

raw = subprocess.run([exe, per, inv], capture_output=True, check=True).stdout
frames, names, pos = [], [], 0
while pos < len(raw):
    nl = raw.index(b'\n', pos)
    name = raw[pos:nl].decode()
    data = raw[nl + 1:nl + 1 + W * H * 3]
    pos = nl + 1 + W * H * 3
    img = Image.frombytes('RGB', (W, H), data).resize((W * SCALE, H * SCALE), Image.NEAREST)
    canvas = Image.new('RGB', (W * SCALE, H * SCALE + LABEL), (50, 50, 60))
    canvas.paste(img, (0, 0))
    ImageDraw.Draw(canvas).text((10, H * SCALE + 8), f'laona {name}', fill=(230, 230, 230))
    frames.append(canvas)
    names.append(name)

# 표정마다 중간쯤 한 장씩 모은 표
picks, seen = [], set()
for i, n in enumerate(names):
    if n not in seen and i + 30 < len(names) and names[i + 30] == n:
        seen.add(n)
        picks.append(frames[i + 30])
cols = 4
rows = (len(picks) + cols - 1) // cols
fw, fh = picks[0].size
sheet = Image.new('RGB', (cols * fw, rows * fh), (50, 50, 60))
for i, f in enumerate(picks):
    sheet.paste(f, ((i % cols) * fw, (i // cols) * fh))
sheet.save(f'{out_dir}/kao{suffix}_sheet.png')

# 모든 표정이 담긴 표에서 64색 팔레트를 뽑아 모든 프레임에 써서 GIF 용량을 줄임
pal = sheet.quantize(64)
gif = [f.quantize(palette=pal, dither=0) for f in frames]
gif[0].save(f'{out_dir}/kao{suffix}.gif', save_all=True, append_images=gif[1:], duration=40, loop=0, optimize=True)
print(f'{out_dir}/kao{suffix}.gif ({len(frames)} frames), {out_dir}/kao{suffix}_sheet.png')
