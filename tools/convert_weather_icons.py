#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from PIL import Image


def parse_hex_color(value: str) -> tuple[int, int, int]:
    raw = value.strip().lstrip('#')
    if len(raw) != 6:
        raise ValueError('bg color must be RRGGBB')
    return tuple(int(raw[i:i + 2], 16) for i in (0, 2, 4))


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def blend_channel(fg: int, bg: int, alpha: int) -> int:
    return (fg * alpha + bg * (255 - alpha)) // 255


def convert_png_to_rgb565_bin(source: Path, target: Path, size: int, bg_rgb: tuple[int, int, int]) -> dict:
    with Image.open(source) as image:
        rgba = image.convert('RGBA')

    rgba.thumbnail((size, size), Image.Resampling.LANCZOS)
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    offset_x = (size - rgba.width) // 2
    offset_y = (size - rgba.height) // 2
    canvas.paste(rgba, (offset_x, offset_y))

    output = bytearray()
    pixels = canvas.load()

    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[x, y]
            rr = blend_channel(r, bg_rgb[0], a)
            gg = blend_channel(g, bg_rgb[1], a)
            bb = blend_channel(b, bg_rgb[2], a)
            color = rgb888_to_rgb565(rr, gg, bb)
            output.append(color & 0xFF)
            output.append((color >> 8) & 0xFF)

    target.write_bytes(output)

    return {
        'name': source.stem,
        'png': source.name,
        'bin': target.name,
        'width': size,
        'height': size,
        'bytes': len(output),
        'format': 'RGB565_LE',
    }


def main() -> None:
    parser = argparse.ArgumentParser(description='Convert weather PNG icons to RGB565 .bin files')
    parser.add_argument('--src', required=True, help='Source directory with PNG files')
    parser.add_argument('--dst', required=True, help='Destination directory for .bin files')
    parser.add_argument('--size', type=int, default=64, help='Output width/height (square), default 64')
    parser.add_argument('--bg', default='000000', help='Background color in RRGGBB, default 000000')
    parser.add_argument('--manifest', default='manifest.json', help='Manifest filename, default manifest.json')
    args = parser.parse_args()

    source_dir = Path(args.src)
    destination_dir = Path(args.dst)
    destination_dir.mkdir(parents=True, exist_ok=True)

    bg_rgb = parse_hex_color(args.bg)
    png_files = sorted(source_dir.glob('*.png'))

    if not png_files:
        raise SystemExit(f'No PNG files found in {source_dir}')

    entries: list[dict] = []
    for png_file in png_files:
        target_file = destination_dir / f'{png_file.stem}.bin'
        entry = convert_png_to_rgb565_bin(png_file, target_file, args.size, bg_rgb)
        entries.append(entry)

    manifest = {
        'source': str(source_dir),
        'output': str(destination_dir),
        'icon_count': len(entries),
        'size': args.size,
        'background_rgb': '#%02X%02X%02X' % bg_rgb,
        'entries': entries,
    }

    manifest_path = destination_dir / args.manifest
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

    print(f'Converted {len(entries)} icons to {destination_dir}')
    print(f'Manifest: {manifest_path}')


if __name__ == '__main__':
    main()
