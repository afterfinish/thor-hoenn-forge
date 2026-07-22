#!/usr/bin/env python3
"""Generate Hoenn Forge ball-wave launcher icons for all densities."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1] / "overlay" / "azahar" / "res"
BG = (22, 24, 38, 255)  # #161826
ACCENT = (145, 132, 217, 255)  # #9184d9
WAVE = (28, 31, 51, 255)


def draw_icon(size: int, for_adaptive_fg: bool = False) -> Image.Image:
    if for_adaptive_fg:
        im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    else:
        im = Image.new("RGBA", (size, size), BG)
    d = ImageDraw.Draw(im)
    cx = cy = size / 2.0
    r = size * 0.30
    stroke = max(2, int(size * 0.045))

    if not for_adaptive_fg:
        for i in range(2):
            y = size * (0.68 + i * 0.12)
            amp = size * 0.05
            pts = []
            for x in range(0, size + 1, max(1, size // 40)):
                yy = y + amp * math.sin((x / max(1, size)) * math.pi * 2 + i)
                pts.append((x, yy))
            if len(pts) > 1:
                d.line(pts, fill=WAVE, width=max(2, size // 28))

    bbox = [cx - r, cy - r, cx + r, cy + r]
    d.ellipse(bbox, outline=ACCENT, width=stroke)

    wave_pts = []
    steps = 56
    for i in range(steps + 1):
        t = i / steps
        x = cx - r * 0.95 + t * r * 1.9
        y = cy + math.sin(t * math.pi * 2) * (r * 0.22)
        if (x - cx) ** 2 + (y - cy) ** 2 <= (r * 0.98) ** 2:
            wave_pts.append((x, y))
    if len(wave_pts) > 1:
        d.line(wave_pts, fill=ACCENT, width=stroke)

    sr = r * 0.16
    d.ellipse([cx - sr, cy - sr, cx + sr, cy + sr], fill=ACCENT)
    return im


def main() -> None:
    densities = {
        "mdpi": 48,
        "hdpi": 72,
        "xhdpi": 96,
        "xxhdpi": 144,
        "xxxhdpi": 192,
    }
    for dens, s in densities.items():
        folder = ROOT / f"mipmap-{dens}"
        folder.mkdir(parents=True, exist_ok=True)
        icon = draw_icon(s, for_adaptive_fg=False)
        icon.save(folder / "ic_launcher.png", optimize=True)
        icon.save(folder / "ic_launcher_round.png", optimize=True)
        fg = draw_icon(s, for_adaptive_fg=True)
        fg.save(folder / "ic_launcher_foreground.png", optimize=True)
        print(dens, s, (folder / "ic_launcher.png").stat().st_size)

    drawable = ROOT / "drawable"
    drawable.mkdir(parents=True, exist_ok=True)
    fg432 = draw_icon(432, for_adaptive_fg=True)
    bg432 = Image.new("RGBA", (432, 432), BG)
    d = ImageDraw.Draw(bg432)
    for i in range(2):
        y = 280 + i * 50
        pts = [(x, y + 18 * math.sin(x / 40.0 + i)) for x in range(0, 433, 4)]
        d.line(pts, fill=WAVE, width=10)
    fg432.save(drawable / "hoenn_ic_launcher_fg.png", optimize=True)
    bg432.save(drawable / "hoenn_ic_launcher_bg.png", optimize=True)
    print("adaptive layers ok")


if __name__ == "__main__":
    main()
