# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

**FRIZZ**, a custom firmware for **CHOMPI** (a discontinued chromatic sampler / tape instrument by
CHOMPI Club / Chase Bliss), forked from CHOMPI's WAVE firmware. Bare-metal C++ for a Daisy Seed2
DFM (STM32H750). It is built on top of CHOMPI's archival open-source release (`upstream` remote),
which will not receive updates.

**[`firmware/README.md`](firmware/README.md) is the developer guide**: toolchain, build, host
checks, the virtual CHOMPI, flashing over USB, the CPU bench, where things are in `code/src`
and the rules the code follows. Read it before changing firmware; this file keeps only the
workflow and what bites. Free memory and how to make room: [`docs/CAPACITY.md`](docs/CAPACITY.md).
The stock firmwares, bootloader and hardware files under `reference/`:
[`docs/STOCK_FIRMWARES.md`](docs/STOCK_FIRMWARES.md).

## Repo layout: user-facing at the root, everything else below it

| Path | What | Audience |
|---|---|---|
| `README.md`, `INSTALL.md`, `QUICKSTART.md`, `MANUAL.md`, `CHANGELOG.md` | what FRIZZ is, install, quick guide, full controls, what changed since the last release | users |
| `firmware/` | FRIZZ source (`code/`, `bin/`, `test/`, `twin/`, `flash.py`, `card.py`, `remote.py`, `tools/`) | developers |
| `docs/` | design notes: `LOOPER.md`, `FX_OVERVIEW.md`, `CAPACITY.md`, `STOCK_FIRMWARES.md` | developers |
| `reference/` | the original CHOMPI release, unchanged except for links | reference only |
| `LICENSE`, `THIRD_PARTY.md`, `TRADEMARKS.md`, `CLAUDE.md` | legal, this file | — |

Keep the root user-facing: anything that only helps development goes in `docs/` or
`firmware/`. When a control changes, update `MANUAL.md` (and `QUICKSTART.md` if it's covered
there). The panel artwork and CHOMPI logos are deliberately absent for copyright reasons, and
the CHOMPI name/marks are excluded from the MIT license (`TRADEMARKS.md`). Don't reintroduce
branding into derived hardware files.

Hand-written source is `firmware/code/src/` (~60 files). Everything under `libs/`,
`cube_dfu/`, `Drivers/`, `Middlewares/` and any `build/` directory is vendored or generated:
exclude it from greps. `reference/` holds three more independent copies of a similar tree, so
scope searches to `firmware/`.

## Git workflow: branches first, main only after testing

Never commit development work to `main`. Start every change (feature, fix, refactor, docs) on
its own branch off `main`, named for what it does (e.g. `loop-length`, `fix-crusher-level`),
and commit and push there. Merge into `main` only after the user has tested the branch (on
hardware where it touches the firmware) and said so; passing `firmware/test/` or a clean build
is not that approval.

**One worktree per branch.** Several sessions work on this repo at once, so never switch
branches in the main checkout (`~/git-projects/FrizZ` stays on `main`). Work on a branch in its
own worktree next to it:

```bash
git fetch && git worktree add ../FrizZ-<branch> -b <branch> origin/main
```

and remove it (`git worktree remove`, `git branch -d`, delete the remote branch) once it's
merged. A tool that sends a build (`flash.py --no-build`) sends that worktree's
`firmware/bin/`: run it from the branch's worktree, or name the file.

Keep **one untested firmware branch at a time**: test it, merge it, and start the next one off
the new `main`. A branch with no firmware change (docs, tooling) doesn't count. A branch still
open when another reaches `main` merges `main` in, so it's tested against what's there. Stack
a branch on an unmerged one only when it really builds on it (or would conflict heavily
without it), and say so in its PR; the top branch's build is then the test build for the
whole stack, its PRs merge bottom-up once it passes, and a fix goes on the branch it belongs
to, merged upwards. Don't start a third level: get the stack tested and merged first.

The branch always carries a built firmware for the user to test: every commit that changes
the bytes of `FRIZZ.bin` rebuilds with `make` and `make BENCH=1` in `firmware/code/src` (GCC
10.3, below) and includes both fresh binaries (`build/FRIZZ.bin`, `build-bench/FRIZZ-bench.bin`)
in `firmware/bin/`, in the same commit. A comment-only commit may leave them, saying in its
message that the md5 is unchanged. On a merge conflict over a binary, rebuild rather than pick
a side. `main` holds the last tested build; releases for users are on GitHub's Releases page.
`firmware/tools/install-hooks.sh` installs a pre-commit hook that refuses commits on `main` and
warns when `firmware/code/` is staged without the binary or `CHANGELOG.md`.

Every commit that changes `firmware/code/`, `firmware/test/` or `firmware/twin/` first passes
`firmware/test/all.sh`. A check that fails is fixed, or, when the change is meant to alter
what it checks, updated in the same commit, saying so in the commit message and in the PR. A
refactor must leave `firmware/test/check.sh` and `firmware/twin/compare.sh` at
`bit-identical`.

**Open topics are GitHub issues** (`gh issue`): a bug, a fault found by a check, a decision
the user still has to make, a test gap, an idea for later. A new finding becomes an issue (the
repo is public: no private details), with the labels `known fault`, `timing`, `cpu`,
`test gap`, `decision` or `parked` where they fit; a PR that settles one says `Fixes #N`, and
a `Known()` check names its issue in its message. Don't keep open topics only in PR texts or
notes.

Two artifacts track every branch; keep both current with each change, and read them before
working on a branch:

- **`CHANGELOG.md`** (Keep a Changelog style): every change a player notices goes under
  **Unreleased** (Added / Changed / Fixed / Removed), in the commit that makes it, worded for
  users like `MANUAL.md`. Credit outside contributors with their PR. Refactoring, tests and
  developer docs stay out unless they change behaviour. A release renames Unreleased to the
  version and its GitHub release notes start from it.
- **A pull request per branch into `main`**, opened (as a draft is fine) once the branch is
  pushed. Its description lists what changed and carries a **hardware test checklist**
  (`- [ ]` items, one per thing the user should try on the CHOMPI, with what to expect).
  Update the description (`gh pr edit`) whenever a commit adds or changes something to test.
  The user ticks the list while testing; merging the PR is the approval to reach `main`.
  **Before merging, every box is ticked or struck through with why** (`~~item~~ deferred by
  the user to …`), so the PR records what was tested. A branch built on another unmerged
  branch either gets a PR covering both or a stacked PR based on that branch (retarget the
  upper PR to `main` before deleting the lower branch, or GitHub closes it). Merge an outside
  contributor's commit unchanged (no squash, rebase or cherry-pick) so GitHub credits them.
- **What the virtual CHOMPI can show, it checks, not the user** (`firmware/twin/`). For each
  thing to test that is about keys, LEDs, the card, levels, clicks or MIDI timing, the branch
  adds a case to `firmware/test/ui.cpp` (or `sync.cpp`/`midi.cpp`), and
  `firmware/twin/ui-at.sh origin/main` shows it failing without the change. `git fetch`
  first: the local `main` can lag. `firmware/twin/compare.sh origin/main HEAD` goes into the
  PR with every difference explained. The PR lists those under **Checked on the twin** (no
  boxes); the hardware checklist keeps what only the device can show: the CPU load and
  crackles, sound judged by ear, the codec, real MIDI, USB and card hardware.

### Test builds

A build handed out for testing goes on GitHub as a **pre-release**, never as Latest:

- Each test round gets a numbered one, `v<next>-beta.N` (next: `v0.12-beta.1`), tagged on the
  branch's pushed head, with that commit's `firmware/bin/FRIZZ.bin` attached and the notes
  taken from `CHANGELOG.md`'s Unreleased section plus a link to the branch's PR. The number
  never moves, so feedback can name the build.
- The pre-release **`beta`** always carries the newest numbered one (until v0.12-beta.1: v0.11
  itself), and moves only with a new one, at a fixed link
  (`https://github.com/ZUHcYa/FrizZ/releases/download/beta/FRIZZ.bin`): `git tag -f beta
  <commit> && git push -f origin beta`, `gh release upload beta firmware/bin/FRIZZ.bin
  --clobber`, and `gh release edit beta` with a title and notes naming the numbered build.
- Cut one only when the user asks for a test build; a release for everyone is a normal
  release on `main`.

## The CHOMPI is shared: ask first

There is one CHOMPI, and other sessions test on it too. **Before anything that uses the real
device** (`flash.py`, `card.py`, `remote.py`, a bench run, or asking the user to try a
build), make sure no other test is running on it, or simply ask the user. Sending replaces a
launcher slot's file: never send to a slot the user didn't name (FRIZZ is on key 10, the bench
on 11, USB storage on 15). The tools reach the device over USB through the multi-firmware
launcher (`firmware/README.md`, *Put it on the CHOMPI*); while it is switched off, they can't.
`firmware/tools/measure.py` measures timing and sound there (a MIDI clock in pairs, a loop's
drift against a TRS clock, an effect's A/B between builds): `firmware/README.md`, *Measure
timing and sound on the device*.

## Toolchain: GCC 10.3, and the pin matters

FRIZZ builds with **GNU Arm Embedded 10.3-2021.10** (GCC 10.3.1): newer GCC intermittently
breaks SD-card communication. On this machine it is at `~/opt/gcc-arm-none-eabi-10.3-2021.10/`,
on `PATH` via `~/.bashrc`; non-interactive shells may not read it, so prepend it explicitly:
`PATH=~/opt/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH make`. Run `make` in
`firmware/code/src`, never from the repo root, and read the memory table it prints. The host
checks need only `g++` and `python3`; the browser twin needs Emscripten in `~/opt/emsdk`.

## Tests: the host checks and the virtual CHOMPI

`firmware/test/all.sh` runs everything (a few minutes): `check.sh` (the engine against HEAD;
a refactor must come out `bit-identical`) and every unit check, `./unit.sh NAME` for one:
`pitch`, `tape`, `crusher`, `freezer`, `scenes`, `store`, `clicks`, `delay`, `controls`, `keys`, `looper`, `tempo`,
`comp`, `level`, `sleep`, `inserts`, `ui`, `remote`, `bench`, `midi`, `sync`. A new check is just a new
`NAME.cpp`; [`firmware/test/README.md`](firmware/test/README.md) says what each covers.
`check.h`'s `Known()` marks a fault found and not yet fixed: it reports, doesn't fail.

`firmware/twin/` is the **virtual CHOMPI**: the whole firmware compiled unchanged for the host
on a simulated board, deterministic and 13-20x real time. `./run.sh -o out.wav -l - SCRIPT`
plays a script of keys, knobs, MIDI and audio from power-on; `web/serve.sh` plays it in the
browser. A bug the user hits comes as such a script (SHIFT + transport writes
`/FRIZZ/bug-N.txt`, `EventLog.h`); once it shows the bug, it becomes a case in `ui.cpp`. The
twin can't show the CPU load, the codec, races between the audio interrupt and `main()`, or
anything else about the chip ([`firmware/twin/README.md`](firmware/twin/README.md)).

## The audio callback is CPU-bound

The callback has 0.5 ms per 24-sample block. Idle it uses ~45 %, a loop with ~11 effects
reaches 100 % and crackles (a known limit, parked by the user). Effects that are off must stay
cheap (`FxGate::Asleep`). A change that only shifts the memory layout has made it crackle on
the device (b5c658c) while the host stayed bit-identical, so suspect the CPU when crackles
appear that the twin can't reproduce. The host can't measure the load; the device can:
`make BENCH=1` builds `FRIZZ-bench.bin` (`Bench.h`, compiled in only then), which runs 22
segments by itself and writes `/FRIZZ/cpu.txt`; `firmware/remote.py load` and `remote.py play
SCRIPT --cpu` read `FRIZZ.bin`'s own load. A branch that touches the engine, the effects or
the memory layout asks for a bench run in its hardware checklist, and its `cpu.txt` goes into
the PR, compared with the last one there. Details: `firmware/README.md`, *Measure the CPU load*.

## Firmware architecture

`chompi_main.cpp` documents the skeleton. Two execution contexts:

1. **`AudioCallback()`**, the audio ISR, once per 24-sample block at 48 kHz: polls controls,
   reads MIDI (`MidiClock.h`), generates UI events, runs the engine.
2. **`MainLoop()`**: UI event dispatch, LEDs, the card, USB answers, battery.

**No file I/O and no blocking call from the audio ISR.** FRIZZ reads the card once at boot and
writes it only from `MainLoop()`: when an FX scene is saved, copied or deleted, 2 s after the
master compressor's knobs, the mono input or the MIDI settings rest (`SceneStore.h`,
`MasterSettings.h`), or when a bug report is asked for (`EventLog.h`). Its files live in
`/FRIZZ` (`frizz_scenes.txt`, `frizz_master.txt`, `.bak` copies, `bug-N.txt`), which
`EnterFrizzDir()` creates at boot on a card without it. MIDI: clock in over TRS and USB
(`MidiClock.h` → `TempoClock.h`, `TapTempo.h` as the fallback), notes, CCs, program changes and
FRIZZ's SysEx (`MidiControl.h`), answered over USB from `MainLoop()`; no other MIDI out.

The play page is `NormalPage.h`; the engine is `passthroughEngine.h` → `Looper.h` +
`FxMorph.h` → `FxChain.h` → `MasterComp.h` → output gain → `limiter.h`. Large buffers live in
SDRAM (`DSY_SDRAM_BSS`), cleared by `ZeroSDRAM()` at boot because startup code doesn't. Card
buffers live in internal RAM, 32-byte aligned, written in whole sectors (`EventLog::Flush`).

**Memory**: the firmware runs from SRAM, loaded by CHOMPI's bootloader (`APP_TYPE=BOOT_SRAM`).
Code has about 23 KB left (`SRAM_EXEC`), the bench build less; `docs/CAPACITY.md` lists every
region and the ways to make room. Never use `make program-boot`, and never swap in upstream
libDaisy: the vendored copy is patched (`THIRD_PARTY.md`).
`__attribute__((optimize("-O0")))` and similar per-function overrides (`ProcessAllControls`)
are deliberate workarounds, not leftovers.
