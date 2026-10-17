#!/usr/bin/env python3
"""preview_icon 프레임에서 장면마다 한 장씩 골라 out/wifi_icon.png 로 모음 (3배 + 아이콘 확대)."""
import subprocess, sys
from PIL import Image, ImageDraw

W, H, SCALE, LABEL = 160, 128, 3, 28
exe, out = sys.argv[1], sys.argv[2]
raw = subprocess.run([exe], capture_output=True, check=True).stdout
scenes, pos = {}, 0
while pos < len(raw):
    nl = raw.index(b'\n', pos)
    name = raw[pos:nl].decode()
    scenes[name] = raw[nl + 1:nl + 1 + W * H * 3]
    pos = nl + 1 + W * H * 3

tiles = []
for name, data in scenes.items():
    img = Image.frombytes('RGB', (W, H), data)
    tile = Image.new('RGB', (W * SCALE + 140, H * SCALE + LABEL), (50, 50, 60))
    tile.paste(img.resize((W * SCALE, H * SCALE), Image.NEAREST), (0, 0))
    zoom = img.crop((W - 20, 0, W, 16)).resize((120, 96), Image.NEAREST)
    tile.paste(zoom, (W * SCALE + 10, 10))
    ImageDraw.Draw(tile).text((10, H * SCALE + 8), name, fill=(230, 230, 230))
    tiles.append(tile)
sheet = Image.new('RGB', (tiles[0].width * 2, tiles[0].height * ((len(tiles) + 1) // 2)), (30, 30, 30))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % 2) * t.width, (i // 2) * t.height))
sheet.save(out)
print(out)
