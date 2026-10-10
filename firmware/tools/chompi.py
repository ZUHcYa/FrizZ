#!/usr/bin/env python3
"""chompi.py: where the CHOMPI on USB is, and getting it from there to where it's wanted.

It is in one of four states, as the computer sees it:

    frizz      FRIZZ, which answers its own SysEx (code/src/MidiControl.h)
    launcher   the multi-firmware launcher's picker, which answers its PING
    storage    the USB storage firmware: the card is a drive labelled CHOMPI-SD
    other      a CHOMPI MIDI device that is neither (an older FRIZZ, TAPE, ...)

and None for nothing on USB. Every way between them goes through the launcher: FRIZZ restarts
into it on its SysEx F0 7D 43 48 10 F7 (MidiClock.h), the storage firmware on an eject, and
the launcher starts a slot on its RUN (05). Launchers and storage firmwares from before those
two need a hand instead, and this says which.

One process at a time has the CHOMPI: the first time a tool reaches for it (state(), a link
to it, a restart), it takes a lock (LOCK, flock) for the process's life, and waits, saying who
has it, while another holds it; what doesn't touch the CHOMPI (measure.py compare) never
waits. A tool started by one that holds it (FRIZZ_CHOMPI_HELD set) shares it; one that talks
to the twin, not the CHOMPI (test/remote.cpp), sets it too. To keep the CHOMPI over several
tools, or for playing it by hand:

    tools/chompi.py hold                 holds it until Ctrl-C
    tools/chompi.py hold CMD ARGS...     holds it while CMD runs (sh -c for a sequence)

Linux only: ALSA's raw MIDI and udisks, Python 3 without packages.
"""
import contextlib
import fcntl
import os
import re
import subprocess
import sys
import time

import midi_send

FRIZZ_SLOT = int(os.environ.get("FRIZZ_SLOT", 10))
BENCH_SLOT = int(os.environ.get("BENCH_SLOT", 11))
TEST_SLOT = int(os.environ.get("TEST_SLOT", 12))  # a branch's build, tested by a session
STORAGE_SLOT = int(os.environ.get("STORAGE_SLOT", 15))
STORAGE_LABEL = "CHOMPI-SD"

RESTART = midi_send.HEADER + bytes([0x10, 0xF7])  # FRIZZ's (MidiClock.h)
SETTINGS = 0x24  # FRIZZ's kCmdSettings: two bytes back; the launcher's BAD_MESSAGE is one
RUN = 0x05
BAD_MESSAGE, BAD_SLOT = 1, 8


def say(*args):
    print(*args, file=sys.stderr, flush=True)


# ---- the lock: one process at a time ------------------------------------------------------

LOCK = os.environ.get("FRIZZ_CHOMPI_LOCK") or os.path.join(
    os.environ.get("XDG_RUNTIME_DIR") or "/tmp", "frizz-chompi.lock")
HELD = "FRIZZ_CHOMPI_HELD"
_lock = None


def claim():
    """Takes the CHOMPI for this process, waiting while another has it"""
    global _lock
    if _lock or os.environ.get(HELD):
        return
    f = open(LOCK, "a+")
    try:
        fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        f.seek(0)
        say("the CHOMPI is in use (%s): waiting" % (f.read().strip() or "?"))
        fcntl.flock(f, fcntl.LOCK_EX)
    f.seek(0)
    f.truncate()
    f.write("pid %d in %s: %s\n" % (os.getpid(), os.getcwd(), " ".join(sys.argv)))
    f.flush()
    _lock = f
    os.environ[HELD] = "1"  # the tools this one starts share it


def hold(cmd):
    """Holds the CHOMPI while cmd runs, or until Ctrl-C without one"""
    claim()
    if cmd:
        sys.exit(subprocess.run(cmd).returncode)
    say("holding the CHOMPI; Ctrl-C lets it go")
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass



# ---- MIDI ---------------------------------------------------------------------------------

@contextlib.contextmanager
def link(node):
    """A midi_send.Link to the raw MIDI node, closed afterwards; takes the CHOMPI first"""
    claim()
    to = midi_send.Link(node)
    try:
        yield to
    finally:
        os.close(to.fd)


def ask_midi(device):
    """frizz, launcher or other, by one query to the device"""
    try:
        with link(device) as to:
            reply = to.call(SETTINGS, timeout=0.3, retries=2, required=False)
    except OSError:
        return None  # not there, or it went away mid-way
    if reply is None:
        return "other"
    return "frizz" if len(reply) >= 2 else "launcher"


def restart(device):
    """Asks a running FRIZZ to restart into the launcher; the launcher ignores it"""
    try:
        with link(device) as to:
            os.write(to.fd, RESTART)
    except OSError:
        pass


# ---- the card, as a drive -----------------------------------------------------------------

def storage_partition():
    """/dev/sdXN of the CHOMPI-SD file system, if it is there"""
    link = "/dev/disk/by-label/" + STORAGE_LABEL
    return os.path.realpath(link) if os.path.exists(link) else None


def storage_disk(part):
    """The whole disk the file system is on: /dev/sdX (a card without partitions: itself)"""
    parent = subprocess.run(["lsblk", "-no", "PKNAME", part], capture_output=True,
                            text=True).stdout.strip()
    return "/dev/" + parent if parent else part


def mountpoint(part):
    with open("/proc/mounts") as mounts:
        for line in mounts:
            fields = line.split()
            if os.path.realpath(fields[0]) == part:
                return fields[1].replace("\\040", " ")
    return None


def udisks(*args, check=True):
    """udisksctl's output; None if it failed and check is off"""
    quiet = [] if args[0] == "info" else ["--no-user-interaction"]  # info takes no such
    result = subprocess.run(["udisksctl", *args, *quiet], capture_output=True, text=True)
    if result.returncode == 0:
        return result.stdout
    if check:
        sys.exit("udisksctl %s: %s" % (" ".join(args), result.stderr.strip()))
    return None


def mount(part, tries=20):
    """Where the card is mounted, mounting it if needed. The drive appears a moment before
    its file system can be mounted, so this tries for a while"""
    for attempt in range(tries):
        point = mountpoint(part)
        if point:
            return point
        out = udisks("mount", "-b", part, check=attempt == tries - 1)
        if out:
            m = re.search(r" at (.+?)\.?$", out.strip())
            return m.group(1) if m else mountpoint(part)
        time.sleep(0.5)


def eject(part):
    """Unmounts the card and ejects the drive, which restarts the storage firmware"""
    os.sync()
    # the desktop looks into a freshly mounted card for a moment (busy): try for a while
    for attempt in range(20):
        if not mountpoint(part) or udisks("unmount", "-b", part, check=attempt == 19):
            break
        time.sleep(0.5)
    disk = storage_disk(part)
    # udisks' Eject runs eject(1) as root: START STOP UNIT with LoEj, what the firmware waits for
    info = udisks("info", "-b", disk)
    m = re.search(r"Drive:\s+'(/org/freedesktop/UDisks2/drives/[^']+)'", info)
    if not m:
        sys.exit("udisks doesn't know the drive behind %s" % disk)
    result = subprocess.run(["gdbus", "call", "--system", "--dest", "org.freedesktop.UDisks2",
                             "--object-path", m.group(1), "--method",
                             "org.freedesktop.UDisks2.Drive.Eject", "{}"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("ejecting %s: %s" % (disk, result.stderr.strip()))


# ---- the state, and the ways between ------------------------------------------------------

def state(device=None):
    """(state, the raw MIDI node or the drive's partition), or (None, None)"""
    claim()
    part = storage_partition()
    if part:
        return "storage", part
    device = device or midi_send.find_device()
    if device:
        found = ask_midi(device)
        if found:
            return found, device
    return None, None


def wait_for(wanted, timeout, device=None, hint=None, hint_after=8):
    """The node (or partition) once the CHOMPI is in the wanted state; exits after timeout"""
    deadline = time.monotonic() + timeout
    hint_at = time.monotonic() + hint_after
    while time.monotonic() < deadline:
        now, where = state(device)
        if now == wanted:
            return where
        if hint and time.monotonic() > hint_at:
            say(hint)
            hint = None
        time.sleep(0.5)
    sys.exit("the CHOMPI didn't get to %s within %d s" % (wanted, timeout))


def to_launcher(timeout=120, device=None):
    """The launcher's raw MIDI node, from wherever the CHOMPI is"""
    now, where = state(device)
    if now == "launcher":
        return where
    if now == "frizz":
        say("restarting FRIZZ into the launcher ...")
        restart(where)
        hint = "no launcher yet: switch the CHOMPI off and on"
    elif now == "storage":
        say("ejecting the card, which restarts the CHOMPI into the launcher ...")
        eject(where)
        hint = ("still no launcher: this USB storage firmware doesn't restart on an eject; "
                "press overdub, then the CHOMPI key")
    elif now == "other":
        say("a CHOMPI that can't be restarted from here: switch it off and on")
        hint = None
    else:
        say("no CHOMPI on USB: switch it on (and connect it)")
        hint = None
    return wait_for("launcher", timeout, device, hint, hint_after=10)


def run(slot, wanted, timeout=120, device=None):
    """Starts slot SLOT from wherever the CHOMPI is and waits for state WANTED (None: don't)"""
    node = to_launcher(timeout, device)
    with link(node) as to:
        reply = to.call(RUN, bytes([slot]), timeout=1.0, retries=2, required=False)
    if reply and reply[0] == BAD_SLOT:
        sys.exit("the launcher has nothing in slot %d" % slot)
    if reply and reply[0] == BAD_MESSAGE:
        say("this launcher can't start a slot from here (no RUN): press key %d" % slot)
    elif reply and reply[0] != 0:
        sys.exit("the launcher refused to start slot %d: status %d" % (slot, reply[0]))
    else:
        say("starting slot %d ..." % slot)
    if wanted:
        return wait_for(wanted, timeout, device)
    return None


def slot_file(node, slot):
    """The file on key SLOT ("" for none), as the launcher at NODE lists it; None from a
    launcher before 1.5, which can't say"""
    with link(node) as to:
        reply = to.call(midi_send.PING, timeout=0.3, retries=3, required=False)
        if not reply or len(reply) < 8 or not reply[7] & midi_send.FEATURE_LIST:
            return None
        reply = to.call(midi_send.LIST, bytes([slot]), timeout=2.0, retries=2,
                        required=False, match=midi_send.for_slot(slot))
    if not reply or reply[0] != 0 or len(reply) < 3:
        return None
    return reply[3:3 + reply[2]].decode("ascii", "replace")


def to_frizz(slot=FRIZZ_SLOT, timeout=120, device=None):
    """FRIZZ's raw MIDI node, starting it from the launcher if it isn't running"""
    now, where = state(device)
    if now == "frizz":
        return where
    return run(slot, "frizz", timeout, device)


def to_storage(timeout=120):
    """The card's mount point, with the CHOMPI in its USB storage firmware"""
    part = storage_partition() or run(STORAGE_SLOT, "storage", timeout)
    return mount(part), part


if __name__ == "__main__":
    if sys.argv[1:2] != ["hold"]:
        sys.exit(__doc__)
    hold(sys.argv[2:])
