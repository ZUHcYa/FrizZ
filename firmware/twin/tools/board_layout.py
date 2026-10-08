#!/usr/bin/env python3
"""board_layout.py: the virtual CHOMPI's panel layout from CHOMPI's own board file.

Reads reference/hardware/hardware-pcb/board files/CC_Chompi_Rev4_1X1.brd (EAGLE XML) and writes
web/layout.json: the board outline, every key (KEY1-28), encoder (SW1-6), the mode switch, and
both WS2812 chains in the order the data runs through them (DOUT -> series resistor -> DIN),
which is the firmware's LED index. Positions in mm, y up as in EAGLE.

Run it again only if the board file changes; the output is committed.
"""
import json
import os
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BRD = os.path.join(REPO, "reference", "hardware", "hardware-pcb", "board files",
                   "CC_Chompi_Rev4_1X1.brd")
OUT = os.path.join(HERE, "..", "web", "layout.json")

root = ET.parse(BRD).getroot()
elements = {e.get("name"): e for e in root.findall(".//elements/element")}
pads, nets = {}, {}
for s in root.findall(".//signals/signal"):
    for c in s.findall("contactref"):
        pads.setdefault(c.get("element"), {})[c.get("pad")] = s.get("name")
        nets.setdefault(s.get("name"), []).append((c.get("element"), c.get("pad")))


def pos(name):
    e = elements[name]
    return {"name": name, "x": round(float(e.get("x")), 2), "y": round(float(e.get("y")), 2)}


def chain(start_net):
    """The LEDs on a data line, in order: each DOUT reaches the next DIN through a resistor"""
    order, net, seen = [], start_net, set()
    while True:
        nxt = None
        frontier = [net]
        for _ in range(3):  # the line itself, then through up to two series parts
            for n in frontier:
                for el, pad in nets.get(n, []):
                    if pad == "DIN" and el.startswith("LED") and el not in seen:
                        nxt = el
                        break
                if nxt:
                    break
            if nxt:
                break
            frontier = [other for n in frontier for el, pad in nets.get(n, [])
                        if el[0] == "R" and el[1:].isdigit()
                        for p2, other in pads[el].items() if p2 != pad]
        if not nxt:
            return order
        order.append(nxt)
        seen.add(nxt)
        out = pads[nxt].get("DOUT") or pads[nxt].get("DO")
        if not out:
            return order
        net = out


# the board outline: the wires on the dimension layer (20)
xs, ys = [], []
for w in root.findall(".//plain/wire"):
    if w.get("layer") == "20":
        xs += [float(w.get("x1")), float(w.get("x2"))]
        ys += [float(w.get("y1")), float(w.get("y2"))]

layout = {
    "source": "reference/hardware/hardware-pcb/board files/CC_Chompi_Rev4_1X1.brd",
    "outline": {"x0": min(xs), "y0": min(ys), "x1": max(xs), "y1": max(ys)},
    # Hardware::SwId's KEY_n is the board's KEYn
    "keys": [pos("KEY%d" % i) for i in range(1, 29)],
    # Hardware::EncoderId's SWn is the board's SWn
    "encoders": [pos("SW%d" % i) for i in range(1, 7)],
    "toggle": pos("SW_NORMAL"),
    # the panel's chain on TIM5 CH4 (PTH) and the keys' on TIM3 CH2 (SMT), firmware index order
    "pth": [pos(n) for n in chain("TIM5_CH4_TOP")],
    "smt": [pos(n) for n in chain("TIM3_CH2_TOP")],
}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    json.dump(layout, f, indent=1)
print("pth", [l["name"] for l in layout["pth"]])
print("smt", [l["name"] for l in layout["smt"]])
print("outline", layout["outline"])
