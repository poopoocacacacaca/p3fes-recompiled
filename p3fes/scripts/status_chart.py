#!/usr/bin/env python3
"""Render p3fes/status/status.toml into p3fes/status/progress.svg (shown in the README).

A square-tile grid in Persona 3 FES colours: deep night blue, sea blue and moonlight cyan. Each tile fills from the
bottom like a rising tide, to the phase's completion.
"""
import pathlib
import tomllib
from xml.sax.saxutils import escape

ROOT = pathlib.Path(__file__).resolve().parents[1]
data = tomllib.loads((ROOT / "status" / "status.toml").read_text())

C = {
    "bg": "#070f26", "bg2": "#0c1d4a", "tile": "#10224f", "edge": "#2a4c9a",
    "done": "#5fe4ff", "partial": "#2f7dff", "todo": "#3a4d7a", "hle": "#a9c8ff",
    "text": "#ffffff", "muted": "#9fb6e6",
}
COLS = 4
GAP = 12
PAD = 24
W = 840
T = (W - 2 * PAD - (COLS - 1) * GAP) / COLS  # tile side
FONT = "'Arial Black','Segoe UI',Helvetica,Arial,sans-serif"

out = []
clip_id = 0


def text(x, y, s, size=13, weight="bold", color=C["text"], anchor="start", italic=False, opacity=1.0):
    style = ' font-style="italic"' if italic else ""
    out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" fill="{color}" '
               f'fill-opacity="{opacity}" text-anchor="{anchor}"{style}>{escape(s)}</text>')


def wrap(s, width):
    words, lines, line = s.split(), [], ""
    for w in words:
        if line and len(line) + 1 + len(w) > width:
            lines.append(line)
            line = w
        else:
            line = f"{line} {w}".strip()
    return lines + [line] if line else lines


def state_color(frac):
    return C["done"] if frac >= 1 else C["partial"] if frac > 0 else C["todo"]


def tile(x, y, w, h, title, frac, big, sub=None, parts=None):
    """A tile with a bottom-up fill. parts = [(frac, colour)] stacks several fills."""
    global clip_id
    clip_id += 1
    cid = f"c{clip_id}"
    out.append(f'<clipPath id="{cid}"><rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6"/></clipPath>')
    out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6" fill="{C["tile"]}"/>')
    level = y + h
    for f, colour in parts or [(frac, state_color(frac))]:
        fh = h * f
        if fh > 0:
            out.append(f'<rect clip-path="url(#{cid})" x="{x:.1f}" y="{level - fh:.1f}" width="{w:.1f}" height="{fh:.1f}" '
                       f'fill="{colour}" fill-opacity="0.32"/>')
            out.append(f'<rect clip-path="url(#{cid})" x="{x:.1f}" y="{level - fh:.1f}" width="{w:.1f}" height="2" '
                       f'fill="{colour}"/>')
        level -= fh
    out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="6" fill="none" '
               f'stroke="{C["edge"]}" stroke-width="1.5"/>')
    size = 64 if w > T * 1.5 else 34
    lines = wrap(title, 26 if w > T * 1.5 else 20)
    for i, line in enumerate(lines):
        text(x + 14, y + 26 + i * 17, line, 14 if w > T * 1.5 else 12.5, color=C["text"])
    text(x + w - 14, y + h - (40 if sub else 16), big, size, "900", C["text"], "end", italic=True)
    if sub:
        for i, line in enumerate(wrap(sub, 42 if w > T * 1.5 else 26)):
            text(x + 14, y + h - 22 + i * 13, line, 10.5, "normal", C["muted"])


phases = data["phase"]
total_w = sum(p["weight"] for p in phases)
overall = sum(p["weight"] * p["done"] for p in phases) / total_w
code = data["code"]
rec = code["recompiled"] / code["functions"]
hle = code["hle"] / code["functions"]
dec = data["decomp"]
dfrac = dec["functions_in_c"] / code["functions"]

# Header
y = PAD
out.append(f'<rect x="{PAD}" y="{y}" width="8" height="34" fill="{C["done"]}" transform="skewX(-12)"/>')
text(PAD + 20, y + 26, "PERSONA 3 FES", 28, "900", italic=True)
text(PAD + 262, y + 26, "NATIVE PORT PROGRESS", 15, "bold", C["done"], italic=True)
text(W - PAD, y + 26, f"updated {data['updated']}", 11, "normal", C["muted"], "end")
y += 50

# Grid: the overall tile spans 2x2, the phases and code tiles fill the rest.
cells = [(c, r) for r in range(64) for c in range(COLS)]
taken = {(0, 0), (1, 0), (0, 1), (1, 1)}
tile(PAD, y, 2 * T + GAP, 2 * T + GAP, "Fully playable port (weighted estimate)", overall, f"{overall * 100:.0f}%",
     sub=f"{sum(1 for p in phases if p['done'] >= 1)} of {len(phases)} phases complete")
items = [(p["name"], p["done"], f'{p["done"] * 100:.0f}%', f'weight {p["weight"]}', None) for p in phases]
items.append(("EE code translated", 1.0, "100%", f'{code["functions"]:,} functions: {rec * 100:.1f}% C++, {hle * 100:.1f}% HLE',
              [(rec, C["done"]), (hle, C["hle"])]))
items.append(("C decompilation (snaku)", dfrac, f"{dfrac * 100:.1f}%", f'{dec["functions_in_c"]:,} functions in C', None))
free = [cell for cell in cells if cell not in taken]
for (name, frac, big, sub, parts), (c, r) in zip(items, free):
    tile(PAD + c * (T + GAP), y + r * (T + GAP), T, T, name, frac, big, sub, parts)
rows = max(r for _, r in free[:len(items)]) + 1
y += rows * (T + GAP) + 10

# Components: small square chips, six per row.
text(PAD, y + 14, "HARDWARE AND SYSTEMS", 14, "900", C["done"], italic=True)
y += 26
CC = 6
cs = (W - 2 * PAD - (CC - 1) * GAP) / CC
comps = data["component"]
for i, comp in enumerate(comps):
    cx = PAD + (i % CC) * (cs + GAP)
    cy = y + (i // CC) * (cs * 0.62 + GAP)
    col = C[comp["state"]]
    out.append(f'<rect x="{cx:.1f}" y="{cy:.1f}" width="{cs:.1f}" height="{cs * 0.62:.1f}" rx="6" fill="{col}" '
               f'fill-opacity="{0.22 if comp["state"] != "todo" else 0.12}" stroke="{col}" stroke-width="1.5"/>')
    out.append(f'<rect x="{cx + 10:.1f}" y="{cy + 10:.1f}" width="9" height="9" fill="{col}" transform="rotate(45 {cx + 14.5:.1f} {cy + 14.5:.1f})"/>')
    for j, line in enumerate(wrap(comp["name"], 15)):
        text(cx + 10, cy + 40 + j * 15, line, 12, "bold")
y += ((len(comps) + CC - 1) // CC) * (cs * 0.62 + GAP) + 14

lx = PAD
for state, label in (("done", "working"), ("partial", "partial"), ("todo", "not yet")):
    out.append(f'<rect x="{lx}" y="{y - 9}" width="10" height="10" fill="{C[state]}" transform="rotate(45 {lx + 5} {y - 4})"/>')
    text(lx + 18, y, label, 11, "normal", C["muted"])
    lx += 96
y += PAD - 4

H = int(y)
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">'
       f'<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{C["bg"]}"/>'
       f'<stop offset="1" stop-color="{C["bg2"]}"/></linearGradient></defs>'
       f'<rect width="{W}" height="{H}" rx="12" fill="url(#bg)"/>'
       f'<circle cx="{W - 70}" cy="70" r="120" fill="{C["done"]}" fill-opacity="0.05"/>'
       + "".join(out) + "</svg>\n")
(ROOT / "status" / "progress.svg").write_text(svg)
print(f"overall {overall * 100:.1f}% -> {ROOT / 'status' / 'progress.svg'}")
