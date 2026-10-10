#!/usr/bin/env python3
"""remote.py: plays and inspects a running FRIZZ over USB MIDI (code/src/MidiControl.h).

    ./remote.py state                 what the play page shows
    ./remote.py leds                  every LED, as the twin's LED log prints them
    ./remote.py load [--every S]      the audio callback's load (max, mean), once or every S s
    ./remote.py settings              the MIDI channel and transport following
    ./remote.py channel N             listen on channel N (1-16), 0 for all
    ./remote.py transport on|off      MIDI Start / Continue / Stop play and pause the loop
    ./remote.py switch up|down|hand   the mode switch: up (settings page), down (play page),
                                      or the real one again; until power-off
    ./remote.py scene get SLOT [FILE] a scene (1-4, 0 the blank one) as JSON
    ./remote.py scene put SLOT FILE   one into slot 1-4, and onto the card
    ./remote.py play SCRIPT [--cpu]   a twin script (twin/README.md) on the device

`play` presses the keys and turns the knobs of a script over FRIZZ's SysEx, at the script's
times, sends its `midi` and `clock` to the device, and checks its `expect led` lines against the
LEDs the device shows; its `toggle 0|1` sets the mode switch (down, up) over SysEx, back to
the real one when the script ends. What only the twin has (the card, the input, the battery, power-on) is
skipped, and `booted` is when the script starts. With --cpu it asks for the load every 250 ms
and prints the worst, the way to try a scenario for crackles on FRIZZ.bin itself rather than on
the bench's build. A bug report (/FRIZZ/bug-N.txt) plays too, from where the device is.

FRIZZ is started first if the CHOMPI is elsewhere: at the launcher's picker, in its USB storage
firmware, or in the bench (tools/chompi.py); --no-start leaves it be, as --device does.

Linux only (ALSA's raw MIDI), Python 3 without packages, like flash.py.
"""
import argparse
import json
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
import chompi  # noqa: E402
import midi_send  # noqa: E402

KEY, TURN, SETTING, SWITCH = 0x11, 0x12, 0x13, 0x14
STATE, PARAMS, LEDS, LOAD, SETTINGS = 0x20, 0x21, 0x22, 0x23, 0x24
SCENE_GET, SCENE_PUT = 0x30, 0x31
SCENE_PARTS, FX_PER_PART = 5, 3

# Hardware::SwId, in order (code/src/EventLog.h's kSwNames)
SW_NAMES = [
    "ENC_1_SW", "ENC_2_SW", "ENC_3_SW", "ENC_4_SW", "ENC_5_SW", "KEY_26", "SW_TOG", "KEY_16",
    "KEY_2", "KEY_3", "KEY_4", "KEY_5", "KEY_17", "KEY_18", "KEY_19", "KEY_1",
    "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_10", "KEY_20", "KEY_21", "KEY_22",
    "KEY_11", "KEY_12", "KEY_13", "KEY_14", "KEY_15", "KEY_23", "KEY_24", "KEY_25",
    "ENC_6_SW", "KEY_27", "KEY_28",
]
FX_NAMES = ["freezer", "shifter", "folder", "crusher", "filter", "flanger", "resonator",
            "slicer", "warble", "tapestop", "delay", "reverb"]
LOOPER = ["empty", "recording", "playing", "paused"]
MODES = ["none", "save", "copy", "delete"]
PAGES = ["output gain", "input gain", "headphone feed"]
# the LED parts kCmdLeds answers: (panel?, first, count)
LED_PARTS = [(True, 0, 10), (False, 0, 9), (False, 9, 8), (False, 17, 8)]


def get14(hi, lo):
    return (hi << 7) | lo


def knob(v):
    """14 bits as a knob's 0-1, 8192 the centre (MidiToKnob)"""
    return v / 16384 if v <= 8192 else 0.5 + 0.5 * (v - 8192) / 8191


def to14(k):
    k = min(1.0, max(0.0, k))
    v = k * 2 * 8192 if k <= 0.5 else 8192 + (k - 0.5) * 2 * 8191
    return int(v + 0.5)


class Frizz:
    def __init__(self, device=None):
        device = device or midi_send.find_device()
        if not device:
            sys.exit("no CHOMPI on USB MIDI")
        self.link = midi_send.Link(device)
        # one write at a time, so messages don't interleave; one query at a time, whose wait
        # for the answer doesn't hold up the clock's ticks or a script's keys
        self.write_lock = threading.Lock()
        self.query_lock = threading.Lock()

    def send(self, cmd, payload=b""):
        self.raw(midi_send.HEADER + bytes([cmd]) + bytes(payload) + b"\xF7")

    def raw(self, data):
        with self.write_lock:
            os.write(self.link.fd, bytes(data))

    def ask(self, cmd, payload=b""):
        with self.query_lock:
            self.send(cmd, payload)
            reply = self.link.recv(cmd, 0.5)
        if reply is None:
            sys.exit("no answer to 0x%02X: is FRIZZ running, a build newer than v0.10?" % cmd)
        return reply

    def leds(self):
        """[(r, g, b)] * 10 for the panel, * 25 for the keys, as the LEDs get them"""
        pth, smt = [], []
        for part, (panel, _, count) in enumerate(LED_PARTS):
            d = self.ask(LEDS, [part])[1:]
            rgb = [tuple(d[3 * i:3 * i + 3]) for i in range(count)]
            (pth if panel else smt).extend(rgb)
        return pth, smt


def led_line(pth, smt):
    """As the twin's LED log: scaled back to 0-255 (PTH x11, SMT x4)"""
    def hexes(leds, by):
        return " ".join("%02x%02x%02x" % tuple(min(255, c * by) for c in rgb) for rgb in leds)
    return "pth " + hexes(pth, 11) + " | smt " + hexes(smt, 4)


def show_state(f):
    d = f.ask(STATE)
    sel, active, flags = d[4], d[5], d[6]
    latched, on = get14(d[8], d[9]), get14(d[10], d[11])
    print("looper     %s, speed %+.3f, at %d%%" % (
        LOOPER[d[0]] if d[0] < 4 else d[0], knob(get14(d[1], d[2])) * 4 - 2,
        round(d[3] / 1.27)))
    print("knobs on   %s" % ("compressor" if sel == len(FX_NAMES) else FX_NAMES[sel]))
    print("scene      %s%s%s, mode %s" % (active - 1 if active else "none",
                                         ", edited" if flags & 1 else "",
                                         ", morphing" if flags & 2 else "", MODES[d[7]]))
    print("on         %s" % " ".join(n for i, n in enumerate(FX_NAMES) if on >> i & 1))
    print("latched    %s" % " ".join(n for i, n in enumerate(FX_NAMES) if latched >> i & 1))
    print("VOLUME     page %s, mix %.2f, out %.2f, in %.2f, headphones %.2f%s" % (
        PAGES[d[12]] if d[12] < len(PAGES) else d[12], knob(get14(d[13], d[14])),
        knob(get14(d[15], d[16])), knob(get14(d[17], d[18])), knob(get14(d[19], d[20])),
        ", mono" if flags & 32 else ""))
    print("tempo      %.1f BPM%s%s" % (get14(d[21], d[22]) / 10,
                                     ", SHIFT held" if flags & 4 else "",
                                     ", erase waiting" if flags & 8 else ""))
    if len(d) > 23:
        print("page       %s; the mode switch stands %s%s" % (
            "settings" if flags & 64 else "play", "up" if d[23] & 1 else "down",
            {0: "", 2: ", SysEx holds it down", 4: ", SysEx holds it up"}.get(d[23] & 6, "")))


def load(f):
    d = f.ask(LOAD)
    return get14(d[0], d[1]) / 10, get14(d[2], d[3]) / 10


def scene_get(f, slot):
    parts = [f.ask(SCENE_GET, [slot, p])[2:] for p in range(SCENE_PARTS)]
    params = {}
    for p in range(SCENE_PARTS - 1):
        for i in range(FX_PER_PART):
            d = parts[p][8 * i:8 * i + 8]
            params[FX_NAMES[p * FX_PER_PART + i]] = [
                round(knob(get14(d[2 * k], d[2 * k + 1])), 6) for k in range(4)]
    last = parts[-1]
    latched = get14(last[1], last[2])
    return {"used": bool(last[0]),
            "latched": [n for i, n in enumerate(FX_NAMES) if latched >> i & 1],
            "params": params}


def scene_put(f, slot, scene):
    for p in range(SCENE_PARTS - 1):
        data = []
        for i in range(FX_PER_PART):
            for k in scene["params"][FX_NAMES[p * FX_PER_PART + i]]:
                v = to14(k)
                data += [v >> 7, v & 0x7F]
        if f.ask(SCENE_PUT, [slot, p] + data)[2] != 0:
            sys.exit("part %d refused" % p)
    latched = sum(1 << FX_NAMES.index(n) for n in scene.get("latched", []))
    if f.ask(SCENE_PUT, [slot, SCENE_PARTS - 1, int(scene.get("used", True)),
                         latched >> 7, latched & 0x7F])[2] != 0:
        sys.exit("refused: slots 1-4 only")


def play(f, path, cpu):
    """A twin script on the device; returns how many expectations failed"""
    start = time.monotonic()
    clock = {"bpm": 0.0}
    stop = threading.Event()
    worst = [0.0, 0.0]
    failed = 0

    def clock_thread():
        nxt = time.monotonic()
        while not stop.is_set():
            bpm = clock["bpm"]
            if bpm <= 0:
                time.sleep(0.005)
                nxt = time.monotonic()
                continue
            nxt += 60.0 / (bpm * 24)
            time.sleep(max(0.0, nxt - time.monotonic()))
            f.raw([0xF8])

    def cpu_thread():
        while not stop.is_set():
            mx, mean = load(f)
            worst[0], worst[1] = max(worst[0], mx), max(worst[1], mean)
            time.sleep(0.25)

    threads = [threading.Thread(target=clock_thread, daemon=True)]
    if cpu:
        load(f)  # from here on
        threads.append(threading.Thread(target=cpu_thread, daemon=True))
    for t in threads:
        t.start()

    def wait_until(t):
        left = start + t / 1000 - time.monotonic()
        if left > 0:
            time.sleep(left)

    def key(name, down):
        if name not in SW_NAMES:
            print("unknown key %s" % name, file=sys.stderr)
            return
        f.send(KEY, [SW_NAMES.index(name), 1 if down else 0])

    skipped = set()
    at_base = 0.0
    toggled = False
    with open(path) as src:
        for line_no, line in enumerate(src, 1):
            words = line.split("#", 1)[0].split()
            if not words or line.startswith("|"):
                continue
            cmd, args = words[0], words[1:]
            now_ms = (time.monotonic() - start) * 1000
            if cmd == "wait":
                wait_until(now_ms + float(args[0]))
            elif cmd == "at":
                wait_until(at_base + float(args[0]))
            elif cmd == "booted":
                at_base = now_ms
            elif cmd in ("down", "up"):
                key(args[0], cmd == "down")
            elif cmd == "tap":
                key(args[0], True)
                time.sleep((float(args[1]) if len(args) > 1 else 60) / 1000)
                key(args[0], False)
            elif cmd == "turn":
                detents = int(args[1])
                while detents:
                    step = max(-64, min(63, detents))
                    f.send(TURN, [int(args[0]), step & 0x7F])
                    detents -= step
            elif cmd in ("midi", "usb"):
                f.raw(int(b, 16) for b in args)
            elif cmd == "clock":
                clock["bpm"] = float(args[0])
            elif cmd == "toggle":
                f.send(SWITCH, [2 if int(args[0]) else 1])
                toggled = True
            elif cmd == "leds":
                print("%7d %s" % (now_ms, led_line(*f.leds())))
            elif cmd == "expect" and args[0] == "led":
                pth, smt = f.leds()
                chain, index, want = args[1], int(args[2]), args[3].lower()
                rgb = (pth if chain == "pth" else smt)[index]
                got = "%02x%02x%02x" % tuple(min(255, c * (11 if chain == "pth" else 4))
                                             for c in rgb)
                if got != want:
                    print("line %d: %s %d is %s, not %s" % (line_no, chain, index, got, want),
                          file=sys.stderr)
                    failed += 1
            else:
                if cmd not in skipped:
                    print("skipped (twin only): %s" % cmd, file=sys.stderr)
                skipped.add(cmd)
    stop.set()
    for t in threads:
        t.join(1)
    if toggled:
        f.send(SWITCH, [0])  # the real switch again
    if cpu:
        mx, mean = load(f)
        print("load: worst max %.1f%%, worst mean %.1f%% of the block"
              % (max(worst[0], mx), max(worst[1], mean)))
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--device", help="the raw MIDI node, if not the first CHOMPI")
    ap.add_argument("--no-start", action="store_true",
                    help="don't start FRIZZ if the CHOMPI is elsewhere")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("state")
    sub.add_parser("leds")
    p = sub.add_parser("load")
    p.add_argument("--every", type=float)
    sub.add_parser("settings")
    p = sub.add_parser("channel")
    p.add_argument("channel", type=int, choices=range(17))
    p = sub.add_parser("transport")
    p.add_argument("on", choices=["on", "off"])
    p = sub.add_parser("switch")
    p.add_argument("pos", choices=["up", "down", "hand"])
    p = sub.add_parser("scene")
    p.add_argument("action", choices=["get", "put"])
    p.add_argument("slot", type=int, choices=range(5))
    p.add_argument("file", nargs="?")
    p = sub.add_parser("play")
    p.add_argument("script")
    p.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    if not a.device and not a.no_start:
        a.device = chompi.to_frizz()
    f = Frizz(a.device)
    if a.cmd == "state":
        show_state(f)
    elif a.cmd == "leds":
        print(led_line(*f.leds()))
    elif a.cmd == "load":
        while True:
            mx, mean = load(f)
            print("max %5.1f%%  mean %5.1f%%" % (mx, mean), flush=True)
            if not a.every:
                break
            time.sleep(a.every)
    elif a.cmd == "settings":
        d = f.ask(SETTINGS)
        print("channel %s, transport %s" % (d[0] or "all", "on" if d[1] else "off"))
    elif a.cmd == "channel":
        f.send(SETTING, [0, a.channel])
    elif a.cmd == "transport":
        f.send(SETTING, [1, 1 if a.on == "on" else 0])
    elif a.cmd == "switch":
        f.send(SWITCH, [{"hand": 0, "down": 1, "up": 2}[a.pos]])
    elif a.cmd == "scene" and a.action == "get":
        text = json.dumps(scene_get(f, a.slot), indent=1)
        if a.file:
            with open(a.file, "w") as out:
                out.write(text + "\n")
        else:
            print(text)
    elif a.cmd == "scene":
        if not a.file:
            sys.exit("scene put SLOT FILE")
        with open(a.file) as src:
            scene_put(f, a.slot, json.load(src))
    elif a.cmd == "play":
        sys.exit(1 if play(f, a.script, a.cpu) else 0)


if __name__ == "__main__":
    main()
