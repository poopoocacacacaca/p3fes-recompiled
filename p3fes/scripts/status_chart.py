#!/usr/bin/env python3
"""Render p3fes/status/status.toml into p3fes/status/progress.svg (shown in the README)."""
import pathlib
import tomllib
from xml.sax.saxutils import escape

ROOT = pathlib.Path(__file__).resolve().parents[1]
data = tomllib.loads((ROOT / "status" / "status.toml").read_text())

W = 820
PAD = 28
BAR_X = 300
BAR_W = W - BAR_X - PAD - 56
ROW = 26
COLORS = {"done": "#2f9e57", "partial": "#d9a21b", "todo": "#9aa3ad", "track": "#e6e9ee", "hle": "#5b8def",
          "text": "#1f2328", "muted": "#59636e", "bg": "#ffffff", "border": "#d0d7de"}

out = []
y = PAD


def text(x, yy, s, size=13, weight="normal", color=COLORS["text"], anchor="start"):
    out.append(f'<text x="{x}" y="{yy}" font-size="{size}" font-weight="{weight}" fill="{color}" '
               f'text-anchor="{anchor}">{escape(s)}</text>')


def bar(yy, parts, label_right):
    out.append(f'<rect x="{BAR_X}" y="{yy - 12}" width="{BAR_W}" height="14" rx="7" fill="{COLORS["track"]}"/>')
    x = BAR_X
    for frac, color in parts:
        w = BAR_W * frac
        if w > 0:
            out.append(f'<rect x="{x:.1f}" y="{yy - 12}" width="{w:.1f}" height="14" rx="7" fill="{color}"/>')
        x += w
    text(BAR_X + BAR_W + 10, yy, label_right, 12, "bold")


phases = data["phase"]
total_w = sum(p["weight"] for p in phases)
overall = sum(p["weight"] * p["done"] for p in phases) / total_w

text(PAD, y + 4, "Persona 3 FES: native port progress", 18, "bold")
text(W - PAD, y + 4, f"updated {data['updated']}", 11, color=COLORS["muted"], anchor="end")
y += 34
text(PAD, y, "Fully playable port (estimate)", 13, "bold")
bar(y, [(overall, COLORS["done"])], f"{overall * 100:.0f}%")
y += ROW + 4
for p in phases:
    color = COLORS["done"] if p["done"] >= 1 else COLORS["partial"] if p["done"] > 0 else COLORS["todo"]
    text(PAD + 14, y, f'{p["name"]}', 12, color=COLORS["muted"])
    bar(y, [(p["done"], color)], f'{p["done"] * 100:.0f}%')
    y += ROW - 4
y += 12

code = data["code"]
rec = code["recompiled"] / code["functions"]
hle = code["hle"] / code["functions"]
text(PAD, y, "EE code translated", 13, "bold")
bar(y, [(rec, COLORS["done"]), (hle, COLORS["hle"])], "100%")
y += 18
text(PAD + 14, y, f'{code["functions"]:,} functions: recompiled to C++ {rec * 100:.1f}%, runtime HLE {hle * 100:.1f}%', 11, color=COLORS["muted"])
y += ROW + 6

dec = data["decomp"]
frac = dec["functions_in_c"] / code["functions"]
text(PAD, y, "C decompilation (snaku)", 13, "bold")
bar(y, [(frac, COLORS["partial"])], f"{frac * 100:.1f}%")
y += 18
text(PAD + 14, y, f'~{dec["functions_in_c"]:,} of {code["functions"]:,} functions in C at {dec["commit"]}; '
                  'the port does not need it', 11, color=COLORS["muted"])
y += ROW + 8

text(PAD, y, "Components", 13, "bold")
y += 12
comps = data["component"]
cols = 3
cw = (W - 2 * PAD) / cols
for i, c in enumerate(comps):
    cx = PAD + (i % cols) * cw
    cy = y + (i // cols) * 26
    out.append(f'<rect x="{cx:.1f}" y="{cy}" width="{cw - 10:.1f}" height="20" rx="10" fill="{COLORS[c["state"]]}" '
               f'fill-opacity="0.16" stroke="{COLORS[c["state"]]}"/>')
    out.append(f'<circle cx="{cx + 12:.1f}" cy="{cy + 10}" r="4" fill="{COLORS[c["state"]]}"/>')
    text(cx + 22, cy + 14, c["name"], 12)
y += ((len(comps) + cols - 1) // cols) * 26 + 10
lx = PAD
for state, label in (("done", "working"), ("partial", "partial"), ("todo", "not yet")):
    out.append(f'<circle cx="{lx + 5}" cy="{y}" r="5" fill="{COLORS[state]}"/>')
    text(lx + 14, y + 4, label, 11, color=COLORS["muted"])
    lx += 90
y += PAD - 6

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{y}" viewBox="0 0 {W} {y}" '
       f'font-family="-apple-system,Segoe UI,Helvetica,Arial,sans-serif">'
       f'<rect x="0.5" y="0.5" width="{W - 1}" height="{y - 1}" rx="10" fill="{COLORS["bg"]}" stroke="{COLORS["border"]}"/>'
       + "".join(out) + "</svg>\n")
(ROOT / "status" / "progress.svg").write_text(svg)
print(f"overall {overall * 100:.1f}% -> {ROOT / 'status' / 'progress.svg'}")
