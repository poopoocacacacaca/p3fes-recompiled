#!/usr/bin/env python3
"""Draw the project's RECOMPILED wordmark banner (p3fes/branding/recompiled.svg).

The wordmark is custom lettering built on a 100-unit cap grid: 22-unit strokes, square counters and one 45-degree
cut on each letter's top-left corner. Secondary text is Inter (SIL OFL), converted to outlines so the SVG renders the
same everywhere. Usage: make_logo.py [path to InterDisplay-Medium.otf]
"""
import pathlib
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

OUT = pathlib.Path(__file__).resolve().parent / "recompiled.svg"
INTER = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/opentype/inter/InterDisplay-Medium.otf"

# --- lettering: (advance width, [polygons]); y grows downwards, cap height 100 ---
GLYPHS = {
    "R": (72, [[(14, 0), (60, 0), (72, 12), (72, 48), (62, 58), (72, 68), (72, 100), (50, 100), (50, 72), (46, 68),
                (22, 68), (22, 100), (0, 100), (0, 14)],
               [(22, 22), (50, 22), (50, 46), (22, 46)]]),
    "E": (68, [[(14, 0), (68, 0), (68, 22), (22, 22), (22, 39), (60, 39), (60, 61), (22, 61), (22, 78), (68, 78),
                (68, 100), (0, 100), (0, 14)]]),
    "C": (68, [[(14, 0), (68, 0), (68, 22), (22, 22), (22, 78), (68, 78), (68, 100), (0, 100), (0, 14)]]),
    "O": (72, [[(14, 0), (72, 0), (72, 86), (58, 100), (0, 100), (0, 14)],
               [(22, 22), (50, 22), (50, 78), (22, 78)]]),
    "M": (96, [[(0, 14), (14, 0), (30, 0), (48, 27), (66, 0), (96, 0), (96, 100), (74, 100), (74, 40), (57, 64),
                (39, 64), (22, 40), (22, 100), (0, 100)]]),
    "P": (72, [[(14, 0), (60, 0), (72, 12), (72, 56), (60, 68), (22, 68), (22, 100), (0, 100), (0, 14)],
               [(22, 22), (50, 22), (50, 46), (22, 46)]]),
    "I": (22, [[(0, 14), (14, 0), (22, 0), (22, 100), (0, 100)]]),
    "L": (66, [[(0, 14), (14, 0), (22, 0), (22, 78), (66, 78), (66, 100), (0, 100)]]),
    "D": (72, [[(14, 0), (56, 0), (72, 16), (72, 84), (56, 100), (0, 100), (0, 14)],
               [(22, 22), (46, 22), (50, 26), (50, 74), (46, 78), (22, 78)]]),
}
TRACK = 17


def word_path(word, x0, y0, scale):
    parts = []
    x = 0.0
    for ch in word:
        adv, polys = GLYPHS[ch]
        for poly in polys:
            pts = " ".join(f"{x0 + (x + px) * scale:.2f},{y0 + py * scale:.2f}" for px, py in poly)
            parts.append(f"M{pts}Z")
        x += adv + TRACK
    return "".join(parts), (x - TRACK) * scale


def word_width(word):
    return sum(GLYPHS[c][0] for c in word) + TRACK * (len(word) - 1)


# --- Inter outlines ---
font = TTFont(INTER)
glyph_set = font.getGlyphSet()
cmap = font.getBestCmap()
upm = font["head"].unitsPerEm


def text_path(text, x, baseline, size, tracking=0.0, anchor="start"):
    scale = size / upm
    advances = [glyph_set[cmap[ord(c)]].width * scale + tracking for c in text]
    total = sum(advances) - tracking
    if anchor == "middle":
        x -= total / 2
    elif anchor == "end":
        x -= total
    pen = SVGPathPen(glyph_set)
    cx = x
    for c, adv in zip(text, advances):
        glyph_set[cmap[ord(c)]].draw(TransformPen(pen, (scale, 0, 0, -scale, cx, baseline)))
        cx += adv
    return pen.getCommands(), total


W, H = 1600, 440
SCALE = 1.42
total = (word_width("RE") + TRACK + word_width("COMPILED")) * SCALE
x0 = (W - total) / 2
cap = 100 * SCALE
y0 = 158
re_path, re_w = word_path("RE", x0, y0, SCALE)
comp_path, _ = word_path("COMPILED", x0 + re_w + TRACK * SCALE, y0, SCALE)

top_text, top_w = text_path("PS2 STATIC RECOMPILATION", W / 2, y0 - 44, 21, tracking=7.5, anchor="middle")
bottom_text, _ = text_path("NATIVE PC PORT", W / 2, y0 + cap + 74, 21, tracking=9, anchor="middle")
rule_y = y0 + cap + 40

svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="RECOMPILED: PS2 static recompilation, native PC port">
<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#050a18"/><stop offset="1" stop-color="#0a1736"/>
  </linearGradient>
  <radialGradient id="moon" cx="0.42" cy="0.40" r="0.62">
    <stop offset="0" stop-color="#16316b" stop-opacity="0.55"/><stop offset="1" stop-color="#0b1a3d" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="streak" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#7fe3ff" stop-opacity="0"/>
    <stop offset="0.5" stop-color="#bff1ff" stop-opacity="0.85"/>
    <stop offset="1" stop-color="#7fe3ff" stop-opacity="0"/>
  </linearGradient>
  <linearGradient id="re" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#4fb2ff"/><stop offset="1" stop-color="#1676f0"/>
  </linearGradient>
  <linearGradient id="white" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#d9e6ff"/>
  </linearGradient>
  <filter id="glow" x="-10%" y="-40%" width="120%" height="180%">
    <feGaussianBlur in="SourceAlpha" stdDeviation="14" result="wide"/>
    <feFlood flood-color="#3fb6ff" flood-opacity="0.55"/><feComposite in2="wide" operator="in" result="g1"/>
    <feGaussianBlur in="SourceAlpha" stdDeviation="3.5" result="tight"/>
    <feFlood flood-color="#9fe4ff" flood-opacity="0.65"/><feComposite in2="tight" operator="in" result="g2"/>
    <feMerge><feMergeNode in="g1"/><feMergeNode in="g2"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  <filter id="soft" x="-5%" y="-300%" width="110%" height="700%"><feGaussianBlur stdDeviation="2.2"/></filter>
</defs>
<clipPath id="frame"><rect width="{W}" height="{H}" rx="18"/></clipPath>
<g clip-path="url(#frame)">
<rect width="{W}" height="{H}" fill="url(#bg)"/>
<circle cx="{W - 250}" cy="150" r="300" fill="url(#moon)"/>
<circle cx="{W - 250}" cy="150" r="228" fill="none" stroke="#5fa8ff" stroke-opacity="0.08" stroke-width="1.5"/>
<path d="{top_text}" fill="#8fb4ec"/>
<rect x="{W / 2 - 700}" y="{y0 + cap * 0.5 - 1.5:.1f}" width="1400" height="3" fill="url(#streak)" filter="url(#soft)" opacity="0.55"/>
<rect x="{W / 2 - 300}" y="{y0 + cap * 0.5 - 0.5:.1f}" width="600" height="1" fill="url(#streak)" opacity="0.9"/>
<g filter="url(#glow)">
  <path d="{re_path}" fill="url(#re)" fill-rule="evenodd"/>
  <path d="{comp_path}" fill="url(#white)" fill-rule="evenodd"/>
</g>
<rect x="{W / 2 - 230}" y="{rule_y:.1f}" width="460" height="1" fill="#2c4f8f"/>
<rect x="{W / 2 - 5}" y="{rule_y - 4.5:.1f}" width="10" height="10" fill="#4fb2ff" transform="rotate(45 {W / 2} {rule_y + 0.5:.1f})"/>
<path d="{bottom_text}" fill="#6f8fc4"/>
</g>
</svg>
"""
OUT.write_text(svg)
print(f"wrote {OUT} ({len(svg) // 1024} KB)")
