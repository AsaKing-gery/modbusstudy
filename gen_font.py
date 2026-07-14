"""Generate 24x24 Chinese font bitmaps for LCD display"""
from PIL import Image, ImageDraw, ImageFont
import os

chars = '自动模式手温度湿二氧化碳氨气加器风机远程畜养殖巡检系统'
SIZE = 24

font_paths = [
    'C:/Windows/Fonts/simsun.ttc',
    'C:/Windows/Fonts/msyh.ttc',
    'C:/Windows/Fonts/simhei.ttf',
]
font = None
for fp in font_paths:
    if os.path.exists(fp):
        try:
            font = ImageFont.truetype(fp, SIZE - 2)
            print(f'// Using font: {os.path.basename(fp)}')
            break
        except Exception as e:
            print(f'// Failed {fp}: {e}')

if font is None:
    print('// ERROR: No Chinese font found!')
    exit(1)

bytes_per_row = (SIZE + 7) // 8
total_bytes = SIZE * bytes_per_row

print(f'// Auto-generated {SIZE}x{SIZE} Chinese font bitmaps (LSB-first)')
print(f'// Total characters: {len(chars)}, bytes/char: {total_bytes}')
print()

lookup = b''
for c in chars:
    try:
        gb = c.encode('gb2312')
        lookup += gb
    except:
        lookup += b'\x00\x00'
        print(f'// WARNING: {c} not in GB2312')

print(f'// Lookup: {lookup.hex().upper()}')
print()

for idx, c in enumerate(chars):
    img = Image.new('1', (SIZE, SIZE), 1)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), c, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (SIZE - tw) // 2 - bbox[0]
    y = (SIZE - th) // 2 - bbox[1]
    draw.text((x, y), c, font=font, fill=0)

    pixels = img.load()
    data = []
    for row in range(SIZE):
        for byte_idx in range(bytes_per_row):
            val = 0
            for bit in range(8):
                col = byte_idx * 8 + bit
                if col < SIZE and pixels[col, row] == 0:
                    val |= (1 << bit)
            data.append(val)

    hex_str = ', '.join(f'0x{b:02X}' for b in data)
    try:
        gb = c.encode('gb2312')
        gb_str = gb.hex().upper()
    except:
        gb_str = '????'
    print(f'  /* [{idx}] {c} GB={gb_str} */')
    print(f'  {{{hex_str}}},')
    print()
