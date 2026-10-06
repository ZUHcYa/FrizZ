#!/usr/bin/env python3
"""compare.py <a.bin> <b.bin>: whether two harness runs are bit-identical, and if not, the
loudness of each segment in both (master out RMS), the peak and any NaNs."""
import math, struct, sys

BLOCK = 24
SEG = int(3 * 48000 / BLOCK)   # blocks per 3s segment
# the FX in harness.cpp's kAll order; the folder, wow & flutter and the tape stop from when
# FRIZZ has them
FX = ["filter", "crusher", "freezer", "slicer", "flanger", "shifter", "resonator"]
SENDS = ["delay", "reverb"]

def layout(n_floats):
    """The harness's meter count from a run's size (one segment per FX, plus 4), and the
    segment names"""
    for meters, fx in ((9, FX), (10, FX + ["folder"]), (12, FX + ["folder", "warble", "tapestop"])):
        if n_floats == (meters + 4) * SEG * (4 * BLOCK + meters):
            return 4 * BLOCK + meters, fx + SENDS + ["all inserts", "everything", "everything 2", "tails"]
    sys.exit(f"unexpected run size: {n_floats} floats")

def load(path):
    data = open(path, "rb").read()
    return struct.unpack(f"{len(data) // 4}f", data)

a_path, b_path = sys.argv[1], sys.argv[2]
if open(a_path, "rb").read() == open(b_path, "rb").read():
    print("bit-identical: outputs and meters")
    sys.exit(0)

a, b = load(a_path), load(b_path)
rec_a, names_a = layout(len(a))
rec_b, names_b = layout(len(b))
print("DIFFERENT" + ("" if names_a == names_b else " (different FX: segments matched by name)"))
print("NaNs:", sum(1 for x in b if x != x))
print(f"{'segment':12s} {'RMS a':>8s} {'RMS b':>8s} {'peak b':>8s}")

def master(v, rec, s):  # master out L/R of segment s
    lo, hi = s * SEG * rec, (s + 1) * SEG * rec
    return [v[i] for i in range(lo, hi) if 2 * BLOCK <= (i - lo) % rec < 4 * BLOCK]

rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v)) if v else 0.0
for s, name in enumerate(names_b):
    vb = master(b, rec_b, s)
    ra = f"{rms(master(a, rec_a, names_a.index(name))):8.4f}" if name in names_a else f"{'-':>8s}"
    print(f"{name:12s} {ra} {rms(vb):8.4f} {max(abs(x) for x in vb):8.4f}")
sys.exit(1)
