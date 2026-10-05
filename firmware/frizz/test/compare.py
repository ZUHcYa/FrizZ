#!/usr/bin/env python3
"""compare.py <a.bin> <b.bin>: whether two harness runs are bit-identical, and if not, the
loudness of each segment in both (master out RMS), the peak and any NaNs."""
import math, struct, sys

BLOCK = 24
REC = 4 * BLOCK + 9            # floats per block: 4 outputs, 9 meters
SEG = int(3 * 48000 / BLOCK)   # blocks per 3s segment
NAMES = ["filter", "crusher", "freezer", "slicer", "flanger", "shifter", "resonator",
         "delay", "reverb", "all inserts", "everything", "everything", "tails"]

def load(path):
    data = open(path, "rb").read()
    return struct.unpack(f"{len(data) // 4}f", data)

a_path, b_path = sys.argv[1], sys.argv[2]
if open(a_path, "rb").read() == open(b_path, "rb").read():
    print("bit-identical: outputs and meters")
    sys.exit(0)

a, b = load(a_path), load(b_path)
print("DIFFERENT")
print("NaNs:", sum(1 for x in b if x != x))
print(f"{'segment':12s} {'RMS a':>8s} {'RMS b':>8s} {'peak b':>8s}")
for s in range((len(b) // REC + SEG - 1) // SEG):
    lo, hi = s * SEG * REC, min((s + 1) * SEG * REC, len(b))
    def master(v):  # master out L/R
        return [v[i] for i in range(lo, min(hi, len(v))) if 2 * BLOCK <= (i - lo) % REC < 4 * BLOCK]
    va, vb = master(a), master(b)
    rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v)) if v else 0.0
    name = NAMES[s] if s < len(NAMES) else str(s)
    print(f"{name:12s} {rms(va):8.4f} {rms(vb):8.4f} {max(abs(x) for x in vb):8.4f}")
sys.exit(1)
