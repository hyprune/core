#!/usr/bin/env python3
"""Contact sheets from `run.sh --reticle` crops (HYPRUNE_RETICLE_SHOTS): one per HUD palette,
rows = states, columns = styles; each crop shown 1:1 (native pixels) and 3x (nearest).
Usage: reticle_sheet.py <shots dir> <out dir>"""
import pathlib, sys
from PIL import Image, ImageDraw, ImageFont

STYLES = ['cross_dot', 't_dot', 'circle_dot', 'dot', 'cross', 'chevron']
STATES = ['neutral', 'target', 'valid', 'out_of_range', 'invalid', 'holding', 'typing']
shots, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
try:
    font = ImageFont.truetype('DejaVuSans.ttf', 22)
except OSError:
    font = ImageFont.load_default()
palettes = sorted({p.name.split('-')[0] for p in shots.glob('*.png')})
for palette in palettes:
    cell, zoom, label = 160, 3, 180
    width, height = label + len(STYLES) * cell * zoom, 40 + len(STATES) * (cell * zoom + 10)
    sheet = Image.new('RGB', (width, height), (24, 26, 30)); draw = ImageDraw.Draw(sheet)
    for c, style in enumerate(STYLES):
        draw.text((label + c * cell * zoom + 10, 8), style, fill=(230, 230, 230), font=font)
    for r, state in enumerate(STATES):
        y = 40 + r * (cell * zoom + 10)
        draw.text((10, y + cell * zoom // 2 - 12), state, fill=(230, 230, 230), font=font)
        for c, style in enumerate(STYLES):
            path = shots / f'{palette}-{state}-{style}.png'
            if not path.exists():
                continue
            crop = Image.open(path).convert('RGB')
            x = label + c * cell * zoom
            sheet.paste(crop.resize((cell * zoom, cell * zoom), Image.NEAREST), (x, y))
            sheet.paste(crop, (x + cell * zoom - cell, y + cell * zoom - cell))  # 1:1 inset, bottom right
    sheet.save(out / f'reticles-{palette}.png')
    print(out / f'reticles-{palette}.png')
