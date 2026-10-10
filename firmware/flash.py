#!/usr/bin/env python3
"""flash.py: puts FRIZZ on the CHOMPI over USB, through the multi-firmware launcher.

    ./flash.py               builds FRIZZ.bin from the working tree, sends it to slot 10
    ./flash.py --bench       builds FRIZZ-bench.bin, sends it to slot 11
    ./flash.py --test        a branch's build for a session's own checks: to slot 12
    ./flash.py --no-build    sends bin/FRIZZ.bin (the branch's committed build) instead
    ./flash.py FILE --slot N sends any .bin to slot N
    ./flash.py --run N       starts what's in slot N already (10 FRIZZ, 11 the bench, 12 a test)

The launcher (github.com/sfaber02/CHOMPI, firmware/chompi-launcher, its releases) is
CHOMPI.bin in the card's root, and keeps the firmwares in /FIRMWARE/NN_NAME.bin, NN being the
key that starts it. While its picker shows, it takes a firmware over USB MIDI, writes it to
its slot, replacing whatever is there, and starts it (tools/midi_send.py, the launcher's
own client). The CHOMPI is first brought to the launcher from wherever it is: a running
FRIZZ restarts into it over USB MIDI (MidiClock.h), the USB storage firmware on an eject
(tools/chompi.py); a FRIZZ from before that, or another firmware, needs the power switch, and
this says so. It then sends, and FRIZZ starts. FRIZZ_SLOT, BENCH_SLOT and TEST_SLOT set other
slots. It waits while another tool has the CHOMPI (tools/chompi.py, the lock).

Linux only (ALSA's raw MIDI), Python 3 without packages; building needs the ARM toolchain
(README.md).
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
import chompi  # noqa: E402
import midi_send  # noqa: E402

SRC = os.path.join(HERE, "code", "src")
TOOLCHAIN = os.path.expanduser("~/opt/gcc-arm-none-eabi-10.3-2021.10/bin")


def build(bench):
    """make (BENCH=1) in code/src with GCC 10.3; the image it made"""
    env = dict(os.environ)
    if os.path.isdir(TOOLCHAIN):
        env["PATH"] = TOOLCHAIN + os.pathsep + env["PATH"]
    try:
        version = subprocess.run(["arm-none-eabi-gcc", "--version"], env=env,
                                 capture_output=True, text=True).stdout
    except FileNotFoundError:
        version = ""
    if " 10.3." not in version:
        sys.exit("needs GNU Arm Embedded 10.3-2021.10 on the PATH (README.md, 1. Toolchain), "
                 "or --no-build")
    args = ["make", "-j8"] + (["BENCH=1"] if bench else [])
    print("building", "FRIZZ-bench.bin" if bench else "FRIZZ.bin")
    result = subprocess.run(args, cwd=SRC, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-3000:] + result.stderr[-3000:])
        sys.exit("the build failed")
    return os.path.join(SRC, "build-bench/FRIZZ-bench.bin" if bench else "build/FRIZZ.bin")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", nargs="?", help="a .bin to send instead of building one")
    ap.add_argument("--bench", action="store_true", help="the CPU bench (FRIZZ-bench.bin)")
    ap.add_argument("--test", action="store_true",
                    help="to the test slot (12), as FRIZZ-TEST: a branch's build, checked by a session")
    ap.add_argument("--no-build", action="store_true", help="send the build in bin/")
    ap.add_argument("--slot", type=int, help="the launcher slot (key) to put it in")
    ap.add_argument("--run", type=int, metavar="SLOT", help="start what's in SLOT, sending nothing")
    ap.add_argument("--name", help="its name on the card (default FRIZZ or FRIZZ-BENCH)")
    ap.add_argument("--wait", type=int, default=120, help="seconds to wait for the launcher")
    ap.add_argument("--device", help="its raw MIDI node, e.g. /dev/snd/midiC1D0 (default: found)")
    args = ap.parse_args()

    if args.run:
        frizz = args.run in (chompi.FRIZZ_SLOT, chompi.TEST_SLOT)
        chompi.run(args.run, "frizz" if frizz else None, args.wait, args.device)
        return
    if args.image:
        image = args.image
    elif args.no_build:
        image = os.path.join(HERE, "bin", "FRIZZ-bench.bin" if args.bench else "FRIZZ.bin")
    else:
        image = build(args.bench)
    slot = args.slot or (chompi.BENCH_SLOT if args.bench
                         else chompi.TEST_SLOT if args.test else chompi.FRIZZ_SLOT)
    name = args.name or ("FRIZZ-BENCH" if args.bench else "FRIZZ-TEST" if args.test else "FRIZZ")
    if args.image and not args.slot:
        sys.exit("a file of your own needs --slot: sending replaces what's in it")

    device = chompi.to_launcher(args.wait, args.device)
    sys.argv = ["midi_send.py", image, "--slot", str(slot), "--name", name, "--device", device]
    midi_send.main()


if __name__ == "__main__":
    main()
