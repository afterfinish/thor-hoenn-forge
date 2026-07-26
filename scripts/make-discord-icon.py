"""Render the Hoenn Forge Discord server icon.

Draws the brand i-mark -- the four-point forge spark over Hoenn's sea wave -- from the
same path data as overlay/azahar/res/drawable/hoenn_mark.xml, so the icon cannot drift
away from the mark used in the app.

Discord crops server icons to a circle and renders them at ~48 px in the sidebar, so the
mark is set inside a safe circle with generous padding and the stroke is kept heavy enough
to survive that downscale. Supersampled 4x because PIL has no round joins; vertices get an
explicit dot instead.

    python scripts/make-discord-icon.py
"""

from __future__ import annotations

from PIL import Image, ImageDraw

# Brand colours, from overlay/azahar/res/values/hoenn_colors.xml
GROUND = (0x16, 0x18, 0x26)
ACCENT = (0x91, 0x84, 0xD9)
ACCENT_GLOW = (0x79, 0x6C, 0xBF)

SIZE = 512
SS = 4  # supersample factor
GRID = 24.0  # the mark is authored on a 24 unit grid
STROKE_UNITS = 2.0

# Four-point forge spark, from the mark's first path.
SPARK = [
    (12.0, 2.6), (13.7, 7.1), (18.2, 8.8), (13.7, 10.5),
    (12.0, 15.0), (10.3, 10.5), (5.8, 8.8), (10.3, 7.1),
]

# How far to lift the wave toward the spark, in grid units.
#
# The mark's own spacing is set for a header lockup, where it sits on a baseline with text.
# In a circular badge that gap reads as a gulf and pushes the wave against the crop: at
# 32 px the wave was crowding the edge while the middle sat empty. The two glyphs are
# untouched -- only the space between them closes.
WAVE_LIFT = 1.4

# Sea wave, from the mark's second path: three cubic segments, the last two smooth.
WAVE = [
    ((3.5, 20.0), (5.4, 17.3), (7.3, 17.3), (9.2, 20.0)),
    ((9.2, 20.0), (11.1, 22.7), (13.0, 22.7), (14.9, 20.0)),
    ((14.9, 20.0), (16.8, 17.3), (18.7, 17.3), (20.6, 20.0)),
]


WAVE = [tuple((x, y - WAVE_LIFT) for (x, y) in seg) for seg in WAVE]


def cubic(p0, p1, p2, p3, steps=48):
    out = []
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        x = u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0]
        y = u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]
        out.append((x, y))
    return out


def stroke(draw, pts, width, colour, closed=False):
    """Polyline with round caps and joins: thick segments plus a dot at every vertex."""
    seq = list(pts) + [pts[0]] if closed else list(pts)
    for a, b in zip(seq, seq[1:]):
        draw.line([a, b], fill=colour, width=int(round(width)))
    r = width / 2.0
    for x, y in seq:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=colour)


def main() -> None:
    w = SIZE * SS
    img = Image.new("RGB", (w, w), GROUND)

    # Soft radial lift behind the mark so the icon does not read as a flat dark square in
    # a sidebar full of bright ones. Drawn as concentric alpha rings, cheap and smooth.
    glow = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    rings = 90
    for i in range(rings, 0, -1):
        t = i / rings
        rad = w * 0.52 * t
        alpha = int(30 * (1.0 - t) ** 2.2)
        if alpha <= 0:
            continue
        gd.ellipse(
            [w / 2 - rad, w / 2 - rad, w / 2 + rad, w / 2 + rad],
            fill=ACCENT_GLOW + (alpha,),
        )
    img = Image.alpha_composite(img.convert("RGBA"), glow).convert("RGB")
    draw = ImageDraw.Draw(img)

    # Fit the mark's ink bounds (paths plus half a stroke) inside the circular safe area.
    wave_pts = [p for seg in WAVE for p in cubic(*seg)]
    xs = [p[0] for p in SPARK + wave_pts]
    ys = [p[1] for p in SPARK + wave_pts]
    pad = STROKE_UNITS / 2.0
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad

    safe = w * 0.66  # Discord masks to a circle; keep the ink well inside it
    scale = safe / max(x1 - x0, y1 - y0)
    off_x = w / 2 - (x0 + x1) / 2 * scale
    off_y = w / 2 - (y0 + y1) / 2 * scale

    def to_px(p):
        return (p[0] * scale + off_x, p[1] * scale + off_y)

    stroke_px = STROKE_UNITS * scale
    stroke(draw, [to_px(p) for p in SPARK], stroke_px, ACCENT, closed=True)
    for seg in WAVE:
        stroke(draw, [to_px(p) for p in cubic(*seg)], stroke_px, ACCENT)

    out = img.resize((SIZE, SIZE), Image.LANCZOS)
    out.save("design/discord-icon.png", "PNG")

    # A circle-masked contact sheet, because the square PNG is not what anyone sees:
    # Discord masks to a circle and renders it at roughly 48 px in the server rail.
    mask = Image.new("L", (w, w), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, w - 1, w - 1], fill=255)
    circ = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    circ.paste(out, (0, 0), mask.resize((SIZE, SIZE), Image.LANCZOS))

    sheet_bg = (0x31, 0x33, 0x38)  # Discord's own rail grey, for an honest preview
    sizes = [128, 80, 48, 32]
    pad = 16
    sheet = Image.new("RGB", (sum(sizes) + pad * (len(sizes) + 1), 128 + pad * 2), sheet_bg)
    x = pad
    for sz in sizes:
        thumb = circ.resize((sz, sz), Image.LANCZOS)
        sheet.paste(thumb, (x, pad + (128 - sz) // 2), thumb)
        x += sz + pad
    sheet.save("design/discord-icon-preview.png", "PNG")
    print("wrote design/discord-icon.png (512) and design/discord-icon-preview.png")


if __name__ == "__main__":
    main()
