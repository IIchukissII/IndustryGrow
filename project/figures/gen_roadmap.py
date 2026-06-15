# -*- coding: utf-8 -*-
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
# Generates the IndustryGrow roadmap dependency map as a standalone SVG.

W, H = 2040, 860

# ---- palette (CEA / instrument-schematic, deliberately not the AI defaults) ----
GROUND   = "#F4F6F5"
INK      = "#16201B"
SLATE    = "#5B6B63"   # secondary lines / text
GREEN    = "#2E6B4F"   # sequential dependency (spine), used quietly; also = resolved risk
RED      = "#B23A2E"   # hidden hard dependency (the signature, bold)
AMBER    = "#C2871F"   # refinement / provisional
BLUE     = "#2F5E8C"   # external system
B1       = "#E6EFE8"   # phase 1 band (faint green)
B2A      = "#EFEDE6"   # phase 2 edge band (warm sand)
B2B      = "#EAEDF0"   # phase 2 cloud band (faint blue)
NODE_FILL= "#FFFFFF"
CARD     = "#FCFDFC"

# ---- geometry ----
def cx(n):            # node centre x
    return 95 + (n-1)*140
NCY   = 300           # node centre y
NW, NH = 116, 78
def nx0(n): return cx(n)-NW//2
def ny0():  return NCY-NH//2          # 261
def ny1():  return NCY+NH//2          # 339

BAND_T, BAND_B = 124, 470
PHASE_X = 585         # phase 1 | phase 2 boundary
SUB_X   = 1425        # edge | cloud sub divider

parts = []
def add(s): parts.append(s)

# header / defs
add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="Helvetica Neue, Arial, sans-serif">')
add('<defs>')
for name,col in [("g",GREEN),("r",RED),("a",AMBER),("b",BLUE),("k",SLATE)]:
    add(f'<marker id="ah-{name}" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7.5" markerHeight="7.5" orient="auto-start-reverse">'
        f'<path d="M0,0 L10,5 L0,10 L3,5 Z" fill="{col}"/></marker>')
add('</defs>')
add('<style>'
    '.disp{font-weight:700}'
    '.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace}'
    'text{fill:'+INK+'}'
    '</style>')

# ground
add(f'<rect x="0" y="0" width="{W}" height="{H}" fill="{GROUND}"/>')

# ---- phase bands ----
add(f'<rect x="36"   y="{BAND_T}" width="{PHASE_X-36}"   height="{BAND_B-BAND_T}" fill="{B1}"  rx="6"/>')
add(f'<rect x="{PHASE_X}" y="{BAND_T}" width="{SUB_X-PHASE_X}" height="{BAND_B-BAND_T}" fill="{B2A}" />')
add(f'<rect x="{SUB_X}" y="{BAND_T}" width="{2004-SUB_X}" height="{BAND_B-BAND_T}" fill="{B2B}" rx="6"/>')

# band eyebrows
add(f'<text class="mono" x="48" y="146" font-size="13" letter-spacing="1.5" fill="{SLATE}">PHASE 1 — DATA COLLECTION ONLY</text>')
add(f'<text class="mono" x="{PHASE_X+12}" y="146" font-size="13" letter-spacing="1.5" fill="{SLATE}">PHASE 2 — EDGE AUTONOMY</text>')
add(f'<text class="mono" x="{SUB_X+12}" y="146" font-size="13" letter-spacing="1.5" fill="{SLATE}">PHASE 2 — CLOUD · SCALE · COMMERCIAL</text>')

# phase boundary (bold) + sub divider (dashed)
add(f'<line x1="{PHASE_X}" y1="{BAND_T}" x2="{PHASE_X}" y2="{BAND_B}" stroke="{INK}" stroke-width="2.6"/>')
add(f'<text class="mono disp" x="{PHASE_X}" y="{BAND_B+22}" font-size="12" text-anchor="middle" fill="{INK}">first actuator = phase change · survey #7 is already Phase 2</text>')
add(f'<line x1="{SUB_X}" y1="{BAND_T}" x2="{SUB_X}" y2="{BAND_B}" stroke="{SLATE}" stroke-width="1.4" stroke-dasharray="5 5"/>')

# ---- title block ----
add(f'<text class="disp" x="40" y="48" font-size="30" letter-spacing="-0.3">IndustryGrow — roadmap as a dependency map</text>')
add(f'<text class="mono" x="40" y="74" font-size="14" fill="{SLATE}">14 typed stages · the map shows real dependencies, not the listed order · v0.1</text>')

# ---- legend (top right) ----
lx, ly = 1180, 40
def legrow(y, draw, label):
    add(draw)
    add(f'<text class="mono" x="{lx+150}" y="{y+5}" font-size="12.5" fill="{INK}">{label}</text>')
add(f'<line x1="{lx}" y1="{ly}" x2="{lx+130}" y2="{ly}" stroke="{GREEN}" stroke-width="2.4" marker-end="url(#ah-g)"/>')
add(f'<text class="mono" x="{lx+150}" y="{ly+5}" font-size="12.5">sequential dependency</text>')
add(f'<line x1="{lx}" y1="{ly+26}" x2="{lx+130}" y2="{ly+26}" stroke="{RED}" stroke-width="2.6" stroke-dasharray="7 5" marker-end="url(#ah-r)"/>')
add(f'<text class="mono" x="{lx+150}" y="{ly+31}" font-size="12.5">hidden dep — needed earlier than scheduled</text>')
add(f'<line x1="{lx}" y1="{ly+52}" x2="{lx+130}" y2="{ly+52}" stroke="{AMBER}" stroke-width="2.6" stroke-dasharray="7 5" marker-end="url(#ah-a)"/>')
add(f'<text class="mono" x="{lx+150}" y="{ly+57}" font-size="12.5">refinement — provisional → final</text>')
add(f'<rect x="{lx}" y="{ly+70}" width="22" height="14" fill="{BLUE}" opacity="0.85" rx="2"/>')
add(f'<text class="mono" x="{lx+150}" y="{ly+82}" font-size="12.5">external system (IndustryFlow)</text>')

# ---- nodes ----
titles = {
 1:["CAN","bring-up"], 2:["DSDL","foundation"], 3:["Sensor","MVP"], 4:["Sensor","platform"],
 5:["Actuator","layer"], 6:["Gateway","MVP"], 7:["Survey","mode"], 8:["Modeling"],
 9:["Profile","system"], 10:["First","cultivation"], 11:["Cloud","integration"],
 12:["Multi-","cabinet"], 13:["Commercial","ready"], 14:["IndustryGrow","v1.0"]}
tags = {1:"ADR-0002·04·17",2:"ADR-0005 *",3:"ADR-0014",4:"ADR-0014·18",5:"ADR-0018 d10",
 6:"ADR-0015",7:"ADR-0016·20",8:"ADR-0016",9:"ADR-0009 *",10:"ADR-0003",11:"ADR-0004·07·20",
 12:"ADR-0001",13:"OTA: no ADR",14:"exit gate"}
# risk discs: node -> (num, colour). GREEN = resolved; RED/AMBER = still open.
risk = {6:("1",AMBER), 7:("2",GREEN), 9:("3",RED)}

for n in range(1,15):
    x0, y0 = nx0(n), ny0()
    add(f'<rect x="{x0}" y="{y0}" width="{NW}" height="{NH}" rx="9" fill="{NODE_FILL}" stroke="{INK}" stroke-width="1.4"/>')
    # stage-number badge (left edge)
    add(f'<circle cx="{x0}" cy="{NCY}" r="14" fill="{INK}"/>')
    add(f'<text class="mono disp" x="{x0}" y="{NCY+4}" font-size="13" text-anchor="middle" fill="{GROUND}">{n}</text>')
    # title (1-2 lines)
    tl = titles[n]
    if len(tl)==1:
        add(f'<text class="disp" x="{cx(n)+6}" y="{NCY+5}" font-size="14.5" text-anchor="middle">{tl[0]}</text>')
    else:
        add(f'<text class="disp" x="{cx(n)+6}" y="{NCY-4}" font-size="13.5" text-anchor="middle">{tl[0]}</text>')
        add(f'<text class="disp" x="{cx(n)+6}" y="{NCY+15}" font-size="13.5" text-anchor="middle">{tl[1]}</text>')
    # ADR tag below
    add(f'<text class="mono" x="{cx(n)}" y="{ny1()+18}" font-size="11" text-anchor="middle" fill="{SLATE}">{tags[n]}</text>')
    # risk disc
    if n in risk:
        num,col = risk[n]
        add(f'<circle cx="{x0+NW-4}" cy="{y0+4}" r="11" fill="{col}" stroke="{GROUND}" stroke-width="1.5"/>')
        add(f'<text class="mono disp" x="{x0+NW-4}" y="{y0+8}" font-size="12" text-anchor="middle" fill="#fff">{num}</text>')

# ---- sequential (green) arrows along the lane ----
def lane_arrow(a, b):
    x1 = nx0(a)+NW
    x2 = nx0(b)
    add(f'<line x1="{x1}" y1="{NCY}" x2="{x2-1}" y2="{NCY}" stroke="{GREEN}" stroke-width="1.9" marker-end="url(#ah-g)"/>')
for a,b in [(1,2),(2,3),(3,4),(4,5),(5,6),(7,8),(8,9),(9,10),(11,12),(12,13),(13,14)]:
    lane_arrow(a,b)

# green skip/branch arcs ABOVE the lane (quiet)
def arc_above(a, b, peak, label, col=GREEN, dash=None, lw=1.9):
    xa = cx(a); xb = cx(b)
    dash_attr = (' stroke-dasharray="%s"' % dash) if dash else ''
    ah = 'g' if col == GREEN else 'k'
    add(f'<path d="M {xa} {ny0()} Q {(xa+xb)/2} {peak} {xb} {ny0()}" fill="none" '
        f'stroke="{col}" stroke-width="{lw}"{dash_attr} marker-end="url(#ah-{ah})"/>')
    add(f'<text class="mono" x="{(xa+xb)/2}" y="{peak-4}" font-size="10.5" text-anchor="middle" fill="{col}">{label}</text>')

arc_above(5,7,247,"survey path (open-loop)")
arc_above(6,10,232,"rule-based control loops", dash="2 4")
arc_above(10,14,216,"≥ 1 full cultivation cycle", dash="2 4")

# ---- backward dependency arcs BELOW the lane (the signature) ----
def arc_below(a, b, peak, col, label, lx_off=0):
    xa = cx(a); xb = cx(b)
    add(f'<circle cx="{xa}" cy="{ny1()}" r="3.4" fill="{col}"/>')
    add(f'<path d="M {xa} {ny1()} Q {(xa+xb)/2} {peak} {xb} {ny1()}" fill="none" '
        f'stroke="{col}" stroke-width="2.5" stroke-dasharray="7 5" marker-end="url(#ah-{ "r" if col==RED else "a"})"/>')
    add(f'<text class="mono disp" x="{(xa+xb)/2 + lx_off}" y="{peak-3}" font-size="11" text-anchor="middle" fill="{col}">{label}</text>')

# 11 -> 7 (survey bulk data has no landing before cloud) — RESOLVED by ADR-0020
#         (local primary sink + survey-capture path); backward arc removed, node-7
#         disc recoloured green. Risk card #2 records the resolution.
# 11 -> 9  (profile signing/versioning/rollback) — still open
arc_below(11,9,402,RED,"signed / versioned / rollback profile", lx_off=70)
# 8 -> 6   (model refines provisional control)
arc_below(8,6,398,AMBER,"model refines the loops")

# ---- external IndustryFlow block (bottom-right, feeds node 11) ----
ex0, ey0, ew, eh = 1636, 360, 360, 70
add(f'<rect x="{ex0}" y="{ey0}" width="{ew}" height="{eh}" rx="7" fill="{BLUE}" opacity="0.92"/>')
add(f'<text class="disp" x="{ex0+14}" y="{ey0+24}" font-size="13.5" fill="#fff">IndustryFlow — external platform work</text>')
add(f'<text class="mono" x="{ex0+14}" y="{ey0+45}" font-size="11" fill="#EAF0F6">mTLS endpoint · production_unit model</text>')
add(f'<text class="mono" x="{ex0+14}" y="{ey0+61}" font-size="11" fill="#EAF0F6">profile distribution API · audit-trail schema</text>')
# arrow from block up-left into node 11
add(f'<path d="M {ex0} {ey0+18} Q {cx(11)+90} {ey0-6} {cx(11)+30} {ny1()+2}" fill="none" stroke="{BLUE}" stroke-width="2.2" marker-end="url(#ah-b)"/>')

# ---- notes panel ----
PN_T = 500
add(f'<line x1="40" y1="{PN_T}" x2="{W-40}" y2="{PN_T}" stroke="{SLATE}" stroke-width="1" stroke-dasharray="3 4"/>')
add(f'<text class="mono disp" x="40" y="{PN_T+24}" font-size="13" letter-spacing="1.5" fill="{INK}">STRUCTURAL RISKS  ·  #2 RESOLVED BY ADR-0020</text>')

cards = [
 ("1", AMBER, "Provisional control",
  ["Loops at #7 are rule-based (ADR-0015 d8); proper tuning",
   "needs the model from #8. Survey #7 runs open-loop and does",
   "NOT need #6.  Natural order: #5→#7→#8→#6/#9→#10."]),
 ("2", GREEN, "Persistence — resolved (ADR-0020)",
  ["Survey #7 needs a durable sink, but cloud lands only at",
   "#11 — so for #1–10 the gateway IS the primary store.",
   "ADR-0020: an SSD/NVMe medium retires the endurance bar; a",
   "time-boxed survey-capture path + bounded store-and-forward",
   "buffer. Local audit log stays rejected (threat model)."]),
 ("3", RED, "Profile is two things",
  ["Local profile file is doable at #6 (ADR-0015 d4); a signed /",
   "versioned / rollback profile needs PKI + IndustryFlow → #11.",
   "First cultivation #10 can run on a plain local file."]),
]
cw = (W-80-2*24)/3
for i,(num,col,head,lines) in enumerate(cards):
    x = 40 + i*(cw+24)
    ch = 150
    add(f'<rect x="{x}" y="{PN_T+36}" width="{cw}" height="{ch}" rx="8" fill="{CARD}" stroke="{col}" stroke-width="1.4"/>')
    add(f'<rect x="{x}" y="{PN_T+36}" width="6" height="{ch}" rx="3" fill="{col}"/>')
    add(f'<circle cx="{x+30}" cy="{PN_T+62}" r="12" fill="{col}"/>')
    add(f'<text class="mono disp" x="{x+30}" y="{PN_T+66}" font-size="13" text-anchor="middle" fill="#fff">{num}</text>')
    add(f'<text class="disp" x="{x+52}" y="{PN_T+67}" font-size="15">{head}</text>')
    for j,ln in enumerate(lines):
        add(f'<text class="mono" x="{x+18}" y="{PN_T+92+j*18}" font-size="11.5" fill="{INK}">{ln}</text>')

# footnote
fy = PN_T + 36 + 150 + 26
add(f'<text class="mono" x="40" y="{fy}" font-size="11.5" fill="{SLATE}">'
    f'* ADR-0005 (DSDL) and ADR-0009 (profile schema) are referenced but not yet audited — must exist before #2 and #9.'
    f'  —  #14 gate also pulls from #10 (one full cycle) and #11 (mTLS, survives net loss + reboot).</text>')

add('</svg>')

svg = "\n".join(parts)
import os
HERE = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(HERE, "industrygrow-roadmap-map.svg")
with open(out,"w",encoding="utf-8") as f:
    f.write(svg)
print("wrote", out, len(svg), "bytes")
