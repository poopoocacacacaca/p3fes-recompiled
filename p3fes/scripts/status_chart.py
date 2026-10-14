#!/usr/bin/env python3
"""Render p3fes/status/status.toml into p3fes/status/progress.svg (shown in the README).

A compact status map: the executable's functions by module and the PS2 hardware as treemaps, and the road to a
playable port as one strip. Colours: Dark Hour green = working, Persona 3 Reload blue = partial, FES red = pending.
Follows the viewer's light/dark theme on GitHub.
"""
import csv
import math
import pathlib
import re
import tomllib
from xml.sax.saxutils import escape

ROOT = pathlib.Path(__file__).resolve().parents[1]
data = tomllib.loads((ROOT / "status" / "status.toml").read_text())

W, PAD, GAP = 900, 16, 12
PANEL_W = (W - 2 * PAD - 2 * GAP) / 2
PANEL_H = 236
out = []


def esc(s):
    return escape(str(s))


def text(x, y, s, size=12, cls="t1", weight="normal", anchor="start", extra=""):
    out.append(f'<text class="{cls}" x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" '
               f'text-anchor="{anchor}"{extra}>{esc(s)}</text>')


def tw(s, size):
    return len(s) * size * 0.58


# --- squarified treemap (Bruls, Huizing, van Wijk) ---
def squarify(values, x, y, w, h):
    total = sum(values)
    if total <= 0:
        return [(x, y, 0, 0) for _ in values]
    scale = w * h / total
    areas = [v * scale for v in values]
    rects, i = [], 0
    while i < len(areas):
        short = min(w, h)
        row = [areas[i]]
        j = i + 1

        def worst(r):
            s = sum(r)
            return max(max(s * s / (short * short * a), short * short * a / (s * s)) for a in r if a > 0)
        while j < len(areas) and worst(row + [areas[j]]) <= worst(row):
            row.append(areas[j])
            j += 1
        s = sum(row)
        if w >= h:  # column on the left
            cw = s / h
            yy = y
            for a in row:
                rects.append((x, yy, cw, a / cw))
                yy += a / cw
            x += cw
            w -= cw
        else:  # row on top
            rh = s / w
            xx = x
            for a in row:
                rects.append((xx, y, a / rh, rh))
                xx += a / rh
            y += rh
            h -= rh
        i = j
    return rects


def cell(x, y, w, h, state, label, tip, size=11, top=0.0):
    """A treemap cell; `top` reserves room for a group chip above the label."""
    out.append(f'<rect class="{state}" x="{x + 1:.1f}" y="{y + 1:.1f}" width="{max(w - 2, 0):.1f}" '
               f'height="{max(h - 2, 0):.1f}"><title>{esc(tip)}</title></rect>')
    if state == "partial":
        out.append(f'<rect x="{x + 1:.1f}" y="{y + 1:.1f}" width="{max(w - 2, 0):.1f}" height="{max(h - 2, 0):.1f}" '
                   f'fill="url(#hatch)" pointer-events="none"/>')
    if label:
        place_label(x, y + top, w, h - top, label, size)


def place_label(x, y, w, h, label, size):
    words = label.replace(" / ", "/ ").split(" ")
    two = [label] if len(words) < 2 else [" ".join(words[:len(words) // 2]), " ".join(words[len(words) // 2:])]
    cx, cy = x + w / 2, y + h / 2
    for sz in (size, size - 1.5, size - 2.5):
        lh = sz * 1.15
        for lines, vertical in (([label], False), (two, False), ([label], True), (two, True)):
            long_side = max(tw(l, sz) for l in lines) + 6
            thick = lh * len(lines) + 4
            fits = (long_side <= w and thick <= h) if not vertical else (long_side <= h and thick <= w)
            if not fits:
                continue
            rot = f' transform="rotate(-90 {cx:.1f} {cy:.1f})"' if vertical else ""
            for i, line in enumerate(lines):
                yy = cy + sz * 0.36 + (i - (len(lines) - 1) / 2) * lh
                text(cx, yy, line, sz, "cell-t", "600", "middle", rot)
            return


def chip(x, y, label):
    w = tw(label, 11) + 10
    out.append(f'<rect class="chip" x="{x + 3:.1f}" y="{y + 3:.1f}" width="{w:.1f}" height="16" rx="3"/>')
    text(x + 8, y + 15, label, 11, "t1", "600")


def panel(x, y, w, h, groups):
    """groups: [(group label, [(name, value, state, tip)])]"""
    out.append(f'<rect class="grupo" x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}"/>')
    outer = squarify([sum(v for _, v, _, _ in items) for _, items in groups], x, y, w, h)
    for (glabel, items), (gx, gy, gw, gh) in zip(groups, outer):
        inner = squarify([v for _, v, _, _ in items], gx + 1, gy + 1, gw - 2, gh - 2)
        chip_w = tw(glabel, 11) + 14
        for (name, _, state, tip), (ix, iy, iw, ih) in zip(items, inner):
            covered = iy <= gy + 2 and ix < gx + chip_w
            cell(ix, iy, iw, ih, state, name, tip, top=18.0 if covered else 0.0)
        out.append(f'<rect class="sep" x="{gx:.1f}" y="{gy:.1f}" width="{gw:.1f}" height="{gh:.1f}" fill="none" '
                   f'stroke-width="2"/>')
        chip(gx, gy, glabel)


STATE_VALUE = {"done": 1.0, "partial": 0.5, "todo": 0.0}


def level(frac):
    return "done" if frac >= 0.999 else "partial" if frac > 0 else "todo"


# --- data ---
code = data["code"]
modules = data["module"]
rows = sorted(csv.reader(open(ROOT / "p3fes_map.csv")), key=lambda r: int(r[1], 16) if r[1].startswith("0x") else -1)
counts = {m["name"]: 0 for m in modules}
prev = modules[0]["name"]
for r in rows:
    if not r[1].startswith("0x"):
        continue
    name = r[0]
    group = next((m["name"] for m in modules if re.match(m["match"], name)), None)
    if group is None:
        group = modules[-1]["name"] if re.search(r"_[0-9a-f]{8}$", name) else prev
    counts[group] += 1
    prev = group
nfun = sum(counts.values())
exercised = sum(counts[m["name"]] * STATE_VALUE[m["state"]] for m in modules) / nfun

hw_groups = data["hardware"]
hw_total = sum(i["weight"] for g in hw_groups for i in g["items"])
hw = sum(i["weight"] * STATE_VALUE[i["state"]] for g in hw_groups for i in g["items"]) / hw_total
phases = data["phase"]
port = sum(p["weight"] * p["done"] for p in phases) / sum(p["weight"] for p in phases)
translated = (code["recompiled"] + code["hle"]) / code["functions"]
decomp = data["decomp"]["functions_in_c"] / code["functions"]

# --- header and badges ---
y = PAD
text(PAD, y + 20, "Status", 22, "t1", "700")
x = W - PAD
for label, frac, shown in reversed([("playable port", port, f"{port * 100:.0f}%"),
                                    ("code translated", translated, f"{translated * 100:.0f}%"),
                                    ("code exercised", exercised, f"{exercised * 100:.0f}%"),
                                    ("hardware", hw, f"{hw * 100:.0f}%"),
                                    ("C decomp", decomp, f"{decomp * 100:.1f}%")]):
    kw, vw = tw(label, 11.5) + 14, tw(shown, 11.5) + 14
    x -= kw + vw
    out.append(f'<rect class="badge-k" x="{x:.1f}" y="{y + 5}" width="{kw + 4:.1f}" height="20" rx="3"/>')
    out.append(f'<rect class="{level(frac)}" x="{x + kw:.1f}" y="{y + 5}" width="{vw:.1f}" height="20" rx="3"/>')
    out.append(f'<rect class="{level(frac)}" x="{x + kw:.1f}" y="{y + 5}" width="4" height="20"/>')
    text(x + kw / 2, y + 19, label, 11.5, "badge-t", "normal", "middle")
    text(x + kw + vw / 2, y + 19, shown, 11.5, "cell-t", "700", "middle")
    x -= 8
y += 34
out.append(f'<line class="borde" x1="{PAD}" y1="{y}" x2="{W - PAD}" y2="{y}" stroke-width="1"/>')
y += 22

# --- panels ---
left_x, right_x = PAD, PAD + PANEL_W + 2 * GAP
text(left_x, y, f"Executable: {nfun:,} functions by module, {exercised * 100:.0f}% exercised", 13, "t1", "600")
text(right_x, y, f"PS2 hardware: {hw * 100:.0f}%", 13, "t1", "600")
y += 8
by_panel = {}
for m in modules:
    tip = f'{m["name"]}: {counts[m["name"]]:,} functions'
    by_panel.setdefault(m["panel"], []).append((m["name"], counts[m["name"]], m["state"], tip))
panel(left_x, y, PANEL_W, PANEL_H,
      [("Middleware + SDK", by_panel["Middleware"]), ("Game code (ATLUS)", by_panel["Game"])])
panel(right_x, y, PANEL_W, PANEL_H,
      [(g["group"], [(i["name"], i["weight"], i["state"], f'{g["group"]} · {i["name"]}') for i in g["items"]])
       for g in hw_groups])
y += PANEL_H + 26

# --- road to playable ---
text(PAD, y, f"Road to a playable port: {port * 100:.0f}%", 13, "t1", "600")
y += 8
total_w = sum(p["weight"] for p in phases)
x = PAD
for p in phases:
    w = (W - 2 * PAD) * p["weight"] / total_w
    st = level(p["done"])
    cell(x, y, w, 26, st, p["name"], f'{p["name"]}: {p["done"] * 100:.0f}%', 10.5)
    x += w
y += 26 + 22

# --- legend ---
x = PAD
for state, label in (("done", "Working"), ("partial", "Partial"), ("todo", "Pending")):
    out.append(f'<rect class="{state}" x="{x}" y="{y - 10}" width="12" height="12"/>')
    if state == "partial":
        out.append(f'<rect x="{x}" y="{y - 10}" width="12" height="12" fill="url(#hatch)"/>')
    text(x + 18, y, label, 12, "t2")
    x += 96
text(W - PAD, y, "Module sizes from p3fes_map.csv (names: snaku decompilation). Hardware weighted per component.",
     10.5, "t2", "normal", "end")
H = int(y + PAD)

css = """
  .fondo { fill: #ffffff; } .borde { stroke: #d0d7de; } .sep { stroke: #ffffff; }
  .t1 { fill: #1f2328; } .t2 { fill: #59636e; } .cell-t { fill: #ffffff; }
  .done { fill: #17a35b; } .partial { fill: #1b8cff; } .todo { fill: #d01f3c; }
  .hatch-line { stroke: #0b4fa3; }
  .chip { fill: #ffffff; fill-opacity: .88; } .badge-k { fill: #3d4a63; } .badge-t { fill: #ffffff; }
  .grupo { fill: #ffffff; }
  @media (prefers-color-scheme: dark) {
    .fondo { fill: #0d1117; } .borde { stroke: #30363d; } .sep { stroke: #0d1117; }
    .t1 { fill: #e6edf3; } .t2 { fill: #9198a1; }
    .chip { fill: #0d1117; fill-opacity: .82; } .grupo { fill: #0d1117; }
  }
"""
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
       f'font-family="-apple-system,BlinkMacSystemFont,\'Segoe UI\',Helvetica,Arial,sans-serif" role="img" '
       f'aria-labelledby="st"><title id="st">Status: playable port {port * 100:.0f}%, code exercised '
       f'{exercised * 100:.0f}%, hardware {hw * 100:.0f}%</title><style>{css}</style>'
       f'<defs><pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
       f'<line class="hatch-line" x1="0" y1="0" x2="0" y2="6" stroke-width="1.6" stroke-opacity=".6"/></pattern></defs>'
       f'<rect class="fondo" width="{W}" height="{H}" rx="6"/>' + "".join(out) + "</svg>\n")
(ROOT / "status" / "progress.svg").write_text(svg)
print(f"port {port * 100:.1f}%  exercised {exercised * 100:.1f}%  hardware {hw * 100:.1f}%  "
      f"modules {counts} -> {ROOT / 'status' / 'progress.svg'}")
