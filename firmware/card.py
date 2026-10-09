#!/usr/bin/env python3
"""card.py: the CHOMPI's card from the computer, over USB, with no hands at the panel.

    ./card.py get                     FRIZZ/cpu.txt and every FRIZZ/bug-N.txt, into card/
    ./card.py get PATH... [-o DIR]    those files (paths on the card) into DIR
    ./card.py put FILE PATH           FILE onto the card as PATH (a folder keeps FILE's name)
    ./card.py ls [PATH]               what's in a folder on the card
    ./card.py mount                   only mounts it, and prints where; `card.py done` ends it
    ./card.py done                    ejects it and starts FRIZZ again

Each brings the CHOMPI from wherever it is (FRIZZ, the launcher's picker) into the USB
storage firmware on the launcher's key 15, mounts the card, does its part, then ejects it,
which restarts the CHOMPI into the launcher, and starts FRIZZ again (slot 10). --then N starts
slot N instead, --then none leaves it at the picker; mount leaves the card mounted.

How it gets there, and which launcher and storage firmware do it without a hand:
tools/chompi.py. FRIZZ_SLOT and STORAGE_SLOT set other slots.

Linux only (ALSA's raw MIDI, udisks), Python 3 without packages, like flash.py.
"""
import argparse
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
import chompi  # noqa: E402


def on_card(root, path):
    """PATH on the card, under its mount point, refusing to leave it"""
    full = os.path.normpath(os.path.join(root, path.lstrip("/")))
    if full != root and not full.startswith(root + os.sep):
        sys.exit("%s is not on the card" % path)
    return full


def short(path):
    rel = os.path.relpath(path)
    return path if rel.startswith("..") else rel


def get(root, paths, out):
    os.makedirs(out, exist_ok=True)
    if not paths:
        paths = ["FRIZZ/cpu.txt"] + sorted(
            os.path.relpath(p, root) for p in glob.glob(os.path.join(root, "FRIZZ", "bug-*.txt")))
    for path in paths:
        src = on_card(root, path)
        if not os.path.isfile(src):
            print("  %s: not on the card" % path)
            continue
        dst = os.path.join(out, os.path.basename(src))
        shutil.copyfile(src, dst)
        print("  %s -> %s" % (path, short(dst)))


def put(root, local, path):
    dst = on_card(root, path)
    if os.path.isdir(dst):
        dst = os.path.join(dst, os.path.basename(local))
    shutil.copyfile(local, dst)
    print("  %s -> %s" % (local, os.path.relpath(dst, root)))


def ls(root, path):
    folder = on_card(root, path or "")
    for name in sorted(os.listdir(folder)):
        full = os.path.join(folder, name)
        print(("%10d  %s" % (os.path.getsize(full), name)) if os.path.isfile(full)
              else "%10s  %s/" % ("", name))


def finish(then):
    """Ejects the card (tools/chompi.py's to_launcher) and starts THEN"""
    if then == "none":
        chompi.to_launcher()
        print("at the launcher's picker")
    else:
        slot = int(then) if then else chompi.FRIZZ_SLOT
        chompi.run(slot, "frizz" if slot == chompi.FRIZZ_SLOT else None)
        print("started slot %d" % slot)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--then", help="the slot to start afterwards (default FRIZZ's), or none")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("get")
    p.add_argument("paths", nargs="*")
    p.add_argument("-o", "--out", default=os.path.join(HERE, "card"))
    p = sub.add_parser("put")
    p.add_argument("file")
    p.add_argument("path")
    p = sub.add_parser("ls")
    p.add_argument("path", nargs="?")
    sub.add_parser("mount")
    sub.add_parser("done")
    a = ap.parse_args()

    if a.cmd == "put" and not os.path.isfile(a.file):
        sys.exit("no file %s" % a.file)
    if a.cmd == "done":
        if not chompi.storage_partition():
            sys.exit("the card isn't on USB")
        finish(a.then)
        return

    root, _ = chompi.to_storage()
    if a.cmd == "mount":
        print(root)
        return
    if a.cmd == "get":
        get(root, a.paths, a.out)
    elif a.cmd == "put":
        put(root, a.file, a.path)
    elif a.cmd == "ls":
        ls(root, a.path)
    finish(a.then)


if __name__ == "__main__":
    main()
