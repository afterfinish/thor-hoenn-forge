#!/usr/bin/env python3
"""Generate Hoenn Forge spark-wave launcher icons for all densities.

The primary launcher art is now the VECTOR adaptive icon
(overlay/azahar/res/drawable/hoenn_ic_launcher_{foreground,background,monochrome}.xml
+ mipmap-anydpi-v26/ic_launcher.xml). These rasters are the pre-API-26 fallback
and are kept visually consistent with the vector mark: a 4-point forge SPARK
above Hoenn's WAVE, in the blurple accent, on a dark ground.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1] / "overlay" / "azahar" / "res"
BG = (22, 24, 38, 255)  # #161826
ACCENT = (145, 132, 217, 255)  # #9184d9
WAVE = (28, 31, 51, 255)  # #1c1f33 background parallax


def draw_icon(size: int, for_adaptive_fg: bool = False) -> Image.Image:
    # Supersample for crisp edges, then downscale (the vector adaptive icon is
    # the primary; this raster is the pre-API-26 fallback).
    ss = 4
    S = size * ss
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0) if for_adaptive_fg else BG)
    d = ImageDraw.Draw(im)
    cx = cy = S / 2.0
    r = S * 0.20            # ball radius
    stroke = max(2, int(S * 0.040))

    # Background parallax waves (full icon only).
    if not for_adaptive_fg:
        for i in range(2):
            y = S * (0.70 + i * 0.14)
            amp = S * 0.05
            pts = [
                (x, y + amp * math.sin((x / S) * math.pi * 2 + i))
                for x in range(0, S + 1, max(1, S // 80))
            ]
            d.line(pts, fill=WAVE, width=max(2, S // 26), joint="curve")

    # Ball silhouette.
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=ACCENT, width=stroke)

    # Wave band through the middle (clipped to the ball).
    steps = 96
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = cx - r + t * 2 * r
        y = cy + math.sin(t * math.pi * 2) * (r * 0.30)
        if (x - cx) ** 2 + (y - cy) ** 2 <= (r * 0.96) ** 2:
            pts.append((x, y))
    if len(pts) > 1:
        d.line(pts, fill=ACCENT, width=stroke, joint="curve")

    # Forge spark-point.
    sr = r * 0.21
    d.ellipse([cx - sr, cy - sr, cx + sr, cy + sr], fill=ACCENT)

    return im.resize((size, size), Image.LANCZOS)


def main() -> None:
    densities = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    for dens, s in densities.items():
        folder = ROOT / f"mipmap-{dens}"
        folder.mkdir(parents=True, exist_ok=True)
        icon = draw_icon(s, for_adaptive_fg=False)
        icon.save(folder / "ic_launcher.png", optimize=True)
        icon.save(folder / "ic_launcher_round.png", optimize=True)
        draw_icon(s, for_adaptive_fg=True).save(
            folder / "ic_launcher_foreground.png", optimize=True
        )
        print(dens, s, (folder / "ic_launcher.png").stat().st_size)

    drawable = ROOT / "drawable"
    drawable.mkdir(parents=True, exist_ok=True)
    draw_icon(432, for_adaptive_fg=True).save(drawable / "hoenn_ic_launcher_fg.png", optimize=True)
    bg = Image.new("RGBA", (432, 432), BG)
    dd = ImageDraw.Draw(bg)
    for i in range(2):
        y = 300 + i * 56
        pts = [(x, y + 20 * math.sin(x / 44.0 + i)) for x in range(0, 433, 4)]
        dd.line(pts, fill=WAVE, width=12, joint="curve")
    bg.save(drawable / "hoenn_ic_launcher_bg.png", optimize=True)
    print("adaptive raster fallback layers ok")


if __name__ == "__main__":
    main()
