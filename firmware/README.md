# FRIZZ firmware: developer guide

The FRIZZ source, how to build it, test it and get it onto a CHOMPI. To install a finished
`FRIZZ.bin`, see [`INSTALL.md`](../INSTALL.md) instead. To find out what the controls do, see
[`MANUAL.md`](../MANUAL.md).

FRIZZ was forked from CHOMPI's WAVE 1.0 firmware (`reference/firmware/chompi-wave/`). Since then
the two have diverged, so treat WAVE as reference, not as a shared core.

## Layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy and DaisySP (patched, MIT; never swap in upstream)
bin/                      FRIZZ.bin (the latest build), FRIZZ-bench.bin (the CPU bench, below),
                          the v6.2 bootloader binary and its
                          install script; the bootloader's source is in
                          reference/firmware/chompi-wave/code/Chompi_Bootloader/
test/                     host-side checks: the engine against HEAD, and unit checks (unit.sh NAME)
twin/                     the virtual CHOMPI: the whole firmware on the PC, on a simulated board
flash.py, card.py, tools/ sending a build to the CHOMPI and reaching its card over USB, through the
                          multi-firmware launcher
```

Design notes live in [`../docs/`](../docs/): the looper spec (`LOOPER.md`) and an overview of
the effects across the CHOMPI firmwares (`FX_OVERVIEW.md`).

## 1. Toolchain

To build the firmware for the CHOMPI, FRIZZ needs **GNU Arm Embedded 10.3-2021.10** (GCC 10.3.1), the Daisy toolchain default. Newer
compilers can build it, but their builds sometimes break SD card communication intermittently.

- **Linux:** download ARM's official tarball (`gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`),
  unpack it, for example to `~/opt/`, and put its `bin/` first on your `PATH`:

  ```bash
  export PATH="$HOME/opt/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH"
  ```

- **macOS:** the [Daisy toolchain installer](https://github.com/electro-smith/DaisyToolchain)
  installs the compiler along with `openocd` and `dfu-util`. If other versions are installed
  too, put 10.3's `bin/` first on your `PATH`.

Don't use a distribution's or Homebrew's `arm-none-eabi-gcc`. It's a much newer GCC and usually
comes without newlib. Check what you have:

```bash
arm-none-eabi-gcc --version    # should say 10.3.1
```

You also need `make`. `dfu-util` is optional (only for reinstalling the bootloader). The checks
on the computer don't need any of this: see [3](#3-test-on-the-host).

## 2. Build

```bash
cd firmware/code/src
make              # or make -j8
make clean        # start fresh
```

The output goes to `code/src/build/`:

- `FRIZZ.bin`: the firmware you put on the SD card
- `FRIZZ.elf`: the same firmware with debug info, for gdb

Read the memory table at the end. FRIZZ runs from SRAM, so a build that compiles can still
fail to link if it doesn't fit. Warnings are normal; only lines that say `error` mean the build
failed.

The prebuilt `libdaisy.a` and `libdaisysp.a` are committed under `code/libs/*/build/`, so you
don't normally rebuild them. If you change a library, run `make` in `code/libs/libDaisy` or
`code/libs/DaisySP` with the same compiler.

## 3. Test on the host

The checks and the virtual CHOMPI build FRIZZ for the computer, not for the CHOMPI, so they
need no ARM toolchain, only:

- **bash, git, coreutils** (`md5sum`), **`g++`** with C++14 and **`python3`** (no packages).
  Tried on Linux (Fedora, GCC 16, Python 3.14). **macOS is untested** and won't work as it
  is: it has no `md5sum`, and the twin's coroutine (`ucontext`) needs `_XOPEN_SOURCE` there.
  Use Linux, a Linux VM or a container until someone ports it.
- For the twin in the browser only: **Emscripten** (below).

Nothing else gets installed: the first run builds DaisySP and the twin for the computer into
`test/build/` and `twin/build/` (ignored by git). From a fresh clone:

```bash
cd firmware/test
./all.sh          # everything, one line each: about 1.5 minutes, the first time a bit more
./check.sh        # engine at HEAD vs the working tree: a refactor must print "bit-identical"
./unit.sh tempo   # one unit check (each NAME.cpp; ui runs the whole firmware on the twin)
```

[`test/README.md`](test/README.md) says what each check covers.

To try a change before flashing it, play it on the virtual CHOMPI: the firmware from
power-on, keys, knobs, MIDI and audio from a script, the master out as a WAV and the LEDs as
text ([`twin/README.md`](twin/README.md)):

```bash
cd firmware/twin
./run.sh -o out.wav -l - examples/filter.txt
web/serve.sh      # the same in the browser, to play and hear: http://localhost:8765
```

Neither shows the CPU load or anything else about the chip; that takes the device.

The browser twin needs Emscripten in `~/opt/emsdk` (or `em++` on the `PATH`); it's been used
with 6.0.11:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/opt/emsdk
cd ~/opt/emsdk && ./emsdk install 6.0.11 && ./emsdk activate 6.0.11
```

`web/serve.sh` finds it there by itself. Its first build compiles DaisySP for the browser too,
so it takes longer; after a change, a reload rebuilds in about 20 s.

**A bug from a player:** SHIFT + transport press on the CHOMPI writes `/FRIZZ/bug-N.txt`
(MANUAL.md, *Bug reports*), a twin script of everything since power-on with the card's files
from then. `twin/run.sh -o out.wav -l leds.txt bug-1.txt` plays it again, up to the combo;
once it shows the bug, turn it into a case in `test/ui.cpp`. What it holds and what not:
[`twin/README.md`](twin/README.md#bug-reports-from-the-device).

## 4. Put it on the CHOMPI

**Over USB, with the multi-firmware launcher** (the quick way while developing). The
[CHOMPI launcher](https://github.com/sfaber02/CHOMPI/releases) by hiwatts, chomplex music theory
and lnetzel sits on the card as `CHOMPI.bin`, keeps firmwares in `/FIRMWARE/NN_NAME.bin` and
starts the one whose key you press (NN). While its picker shows, it takes a firmware over USB
MIDI, writes it to its slot and starts it:

```bash
cd firmware
./flash.py            # builds FRIZZ.bin, restarts the CHOMPI into the launcher, sends it to slot 10
./flash.py --bench    # FRIZZ-bench.bin to slot 11
./flash.py --no-build # bin/FRIZZ.bin as committed
./flash.py --run 11   # starts what's in slot 11 already, sending nothing
```

Sending replaces whatever is in that slot, so set yours (`FRIZZ_SLOT=4 ./flash.py`,
`BENCH_SLOT`, or `--slot`) if FRIZZ isn't on key 10. Linux only (ALSA), Python 3, no
packages; it uses the launcher's own client, `tools/midi_send.py`. On macOS or Windows, the
launcher's web page (https://ugrossek.github.io/CHOMPI/, Chrome or Edge) does the same.
FRIZZ keeps its files in `/FRIZZ`, so it shares the card with the other firmwares.

**The card over USB.** The launcher's key 15 is lnetzel's USB storage firmware: the card shows
up as a drive. `./card.py` uses it with no hands at the panel:

```bash
./card.py get                  # FRIZZ/cpu.txt and every bug report, into card/
./card.py get FRIZZ/x.txt -o . # any file
./card.py put FILE FRIZZ/      # a file onto the card
./card.py ls FRIZZ             # a folder
./card.py mount                # just mounts it and prints where; ./card.py done ends it
```

Each brings the CHOMPI into the storage firmware, mounts the card, does its part, ejects it and
starts FRIZZ again (`--then N` another slot, `--then none` stays at the picker), about 30 s in
all. `remote.py` likewise starts FRIZZ if the CHOMPI is elsewhere, and `flash.py` sends from
wherever it is.

All of them go through the launcher (`tools/chompi.py`): a running FRIZZ restarts into it over
USB MIDI (the SysEx `F0 7D 43 48 10 F7`, `MidiClock.h`), the storage firmware restarts on an
eject, and the launcher starts a slot on its `RUN` command. The last two are in
[launcher/run-slot](https://github.com/ZUHcYa/FrizZ/tree/launcher/run-slot) and
[usb-storage/restart-on-eject](https://github.com/ZUHcYa/FrizZ/tree/usb-storage/restart-on-eject),
offered upstream; with the released launcher v1.3 the tools still work, but say which key to
press (15 for the card, overdub then CHOMPI to leave it, 10 for FRIZZ). With an older FRIZZ or
another firmware running, switch the CHOMPI off and on when they say so (they wait up to 2
minutes). Mounting needs udisks, as any desktop has.

**Remote control over USB MIDI.** `./remote.py` talks to a running FRIZZ over its SysEx
(`code/src/MidiControl.h`): `state`, `leds`, `load [--every S]`, `settings`, `channel N`,
`transport on|off`, `scene get|put SLOT FILE`, and `play SCRIPT [--cpu]`, which plays a twin
script (`twin/scenarios/`, a bug report) on the device: its keys and knobs over SysEx at the
script's times, its MIDI and clock, its `expect led` lines checked against the device's LEDs.
With `--cpu` it reports the worst load FRIZZ.bin itself had, measured in the audio callback as
the bench does, so a scenario can be tried for crackles on the build that plays, not the
bench's. Linux only, Python 3, no packages, like `flash.py`.

**From the card:** copy `build/FRIZZ.bin` to the SD card and power on, as described in
[`INSTALL.md`](../INSTALL.md). FRIZZ is a `BOOT_SRAM` app: CHOMPI's bootloader copies it from
the card into QSPI flash and runs it from SRAM. Standard Daisy flashing advice doesn't apply.

Every CHOMPI already has the bootloader, and an SD update never touches it. Only a blank or
erased Daisy Seed needs it installed. To do that, hold BOOT and tap RESET to enter DFU mode,
then run `bin/install_bootloader.sh`.

## Measure the CPU load

The audio callback has 0.5 ms per block, and neither the host checks nor the virtual CHOMPI
can tell how much of it a change uses. The CPU bench can, on the device:

```bash
cd firmware/code/src
make BENCH=1      # build-bench/FRIZZ-bench.bin; the normal build is untouched
```

1. With the launcher, `./flash.py --bench` puts it on its own key (11) and starts it. Without
   it, put `FRIZZ-bench.bin` (this one, or `bin/FRIZZ-bench.bin`) on the card instead of
   `FRIZZ.bin` (the bootloader takes the first `.bin` it finds, whatever its name) and switch
   on. The bootloader flashes it as usual.
2. After the boot animation the bench waits about 10 s for every effect to rest (only CHOMPI
   glows dimly), then runs by itself, about 90 s: between segments it waits for the tails of
   effects the next one doesn't use to ring out. Don't touch anything: the keys light up one
   per segment, green below 80% load, amber below 95%, red above; the one running blinks white.
3. At the end every panel LED is green (all below 95%) or red. Blinking red: `cpu.txt`
   couldn't be written (no card?).
4. On the computer (with the launcher: `./card.py get`), `FRIZZ/cpu.txt` has every segment's highest and mean load, any effect
   still working outside its segment (a tail), and whether the loop played. Below, where each
   segment's mean goes: the clock the chip ran at, and the mean split into the callback's
   parts (MIDI, the controls, the UI's events, the input, the looper, the tempo, the FX chain,
   the compressor, the output and limiters), counted in the core's cycles (`BenchProfile.h`).
   Put `FRIZZ.bin` back on the card to play again.

The segments: nothing on, each effect alone (its knobs moving every 0.25 s), the compressor,
the inserts together, recording a loop, the loop alone, with the inserts, with the delay, with
PR #7's scene 4 (shifter +7, folder, crusher, slicer, compressor at 1) and the delay, with the
reverb, then everything, everything with every feedback at the top (the harness's STRESS), and
the loop with everything. The sends come last because their tails run on (the delay's for 10
s). The signal is the bench's own, so runs compare: a little tune in A minor at 120 BPM (Am F
C G, an arpeggio, bass, kick, hi-hat), made by a few oscillators, so it costs no room for a
recording. It's made for the next block after each measurement, so its own cost isn't in the
numbers (it was in the first runs', 2026-10-08, with a saw, a gated sine and noise).

The bench measures `FRIZZ-bench.bin`, whose memory layout differs from `FRIZZ.bin`'s; a
crackle from the layout alone (b5c658c) can show in one and not the other. It tells what the
code costs; whether `FRIZZ.bin` crackles, only playing it tells.

## Before a pull request

Work on a branch off `main`, never on `main` itself, and open a pull request for it (a draft
is fine). Before each commit that touches `code/`, `test/` or `twin/`:

1. **`test/all.sh` passes.** A check that fails is fixed, or, when the change is meant to
   alter what it checks, updated in the same commit, saying so in the commit message.
2. **A refactor changes nothing:** `test/check.sh` and `twin/compare.sh` print
   `bit-identical`.
3. **What the twin can show, a check shows:** for a change to keys, LEDs, the card, levels or
   clicks, add a case to `test/ui.cpp` and make sure `twin/ui-at.sh origin/main` fails it (it
   passes with your change). `git fetch` first: a stale local `main` compares with an old
   version.
4. **The binaries match the source:** a change under `code/` rebuilds both and commits them
   with it: `make` and `make BENCH=1` in `code/src`, then `build/FRIZZ.bin` and
   `build-bench/FRIZZ-bench.bin` to `bin/`.
5. **Players read about it:** what they notice goes into [`CHANGELOG.md`](../CHANGELOG.md)
   under *Unreleased*, and a changed control into [`MANUAL.md`](../MANUAL.md).

The pull request lists what changed, what the twin checked (with `twin/compare.sh origin/main
HEAD`'s output, every difference explained), and a checklist of what only the device can
show: the CPU load and crackles (with a bench run's `cpu.txt` when the engine, the effects
or the memory layout changed), the sound by ear, the codec, real MIDI, USB and the card.
[`CLAUDE.md`](../CLAUDE.md) has the full workflow, including test builds and releases.

## 5. Debug

This needs an STLINK-V3MINIE and a Daisy Seed with a soldered debug header. The back panel
doesn't fit over the header, so a second Seed for development is handy.

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg     # terminal 1
arm-none-eabi-gdb code/src/build/FRIZZ.elf                   # terminal 2
(gdb) target remote localhost:3333
```

If a CHOMPI stops responding and nothing else works, reinstall CHOMPI's v6.2 bootloader with
`bin/install_bootloader.sh` (or flash `bin/CHOMPI_Bootloader_V6_2_0.bin` to `0x08000000` with
an ST-Link), then reinstall FRIZZ from the card. The web tool at https://flash.daisy.audio can
erase a stuck Seed, but the generic Daisy bootloader it offers is not CHOMPI's.

## Where things are in `code/src`

```
chompi_main.cpp        entry point: audio callback, main loop, boot sequence
passthroughEngine.h    the engine: input gain, the looper, input/loop mix ("dry/wet" in the code), punch-in FX,
                       master compressor, output gain, headphone cue, safety limiter
FxChain.h              the punch-in effects in their processing order, with a level meter each
FxParams.h             each effect's knobs: how many, defaults, steps, coarse grids; the compressor's too
FxSlots.h              each effect's key, LED and colours; the compressor's key
MasterComp.h           the master compressor: amount, ratio, speed, mix, stereo-linked
MasterSettings.h       what's kept on the card outside the scenes (the compressor's knobs and the
                       mono input), and its file format
SceneStore.h           the card: /FRIZZ, the scene file and the master file, written from MainLoop
FxScenes.h             the FX scenes and their file format
FxControls.h           the FX keys and knobs: latches, fine / stepped / coarse turns, scene snapshot and recall;
                       the compressor's knobs too
SceneControls.h        the scene keys: recall, morph, and the save / copy / delete flow
FxMorph.h              a scene morph: glides the effects to a scene, landing on a bar line
PlayKeys.h             the CHOMPI, PLAY and LOOP keys: SHIFT, the confirm tap, the looper's combos
FxCommon.h             what the effects share: the key's fade, smoothed settings, the base class,
                       tone lowpass, press envelope, stereo delay line
Fx*.h                  one effect each: Freezer, Shifter, Folder, Crusher, Filter, Flanger,
                       Resonator, Slicer, Warble (wow & flutter), TapeStop, Delay, Reverb
LICENSE-kastle2        the MIT license of the effects ported from Bastl's Kastle 2 FX Wizard
LedColors.h            the LED colours
DJFilter.h, BasicMMF.h WAVE's DJ filter
granularDelay.h        TEMPO's tempo-synced delay, without its freeze
reverb.h, fx_engine.h  TEMPO's reverb
TempoClock.h           the tempo and the shared 12 PPQN pulse position for the clocked effects: from the
                       loop, MIDI clock, taps or free running
TapTempo.h             tap tempo
Looper.h               the looper: recording, quantized end, playback, speed, scrub
MidiClock.h            MIDI clock input over TRS and USB; hands the rest to MidiControl.h
MidiControl.h          MIDI control: notes as keys, CCs, program changes, FRIZZ's SysEx (keys,
                       knobs, queries answered over USB for remote.py)
NormalPage.h           the play page: routes the controls (VOLUME, PLAY/LOOP, transport, FX and scene keys) and draws the LEDs
LedSignal.h            the play page's short LED signals: 3 red or white blinks, a flash
ui.h                   page plumbing: events, page switching
limiter.h, EnvFollower.h
                       the safety limiter and the VU meter
hardware.h             the CHOMPI hardware: encoders, keys, switches, LEDs, battery
encoder.h / .cpp       encoder driver
temp_led_stuff.h       LED driver (ui_utils.h: LED flush/clear helpers)
BootPage.h, RainbowWavePage.h
                       boot animation, rainbow-wave animation
chompi_sram.lds        linker script (the firmware runs from SRAM, placed there by the bootloader)
```

## Rules the code follows

- **No file I/O and no blocking calls in the audio callback.** FRIZZ reads the SD card once at
  boot (the FX scenes and the compressor's settings, after changing into `/FRIZZ`, which it
  creates on a new card) and writes it only from `MainLoop`, when a scene is saved, copied or
  deleted, or 2 s after the compressor's knobs, the mono input or the MIDI settings were last
  changed (`SceneStore.h`).
- **MIDI is read in the audio callback, acted on in `MainLoop`.** `MidiControl.h` only notes
  what came (keys, detents, a table of controller values); the play page takes the rest in
  `MainLoop` and answers queries over USB from there, never from the interrupt.
- **Card buffers:** FatFs hands the whole sectors of a read or write straight between your
  buffer and the SD card's DMA. So a buffer for the card lives in internal RAM (a global or a
  member of one: not on the stack, which is in DTCM, and not in SDRAM, neither of which the
  DMA reaches reliably), is aligned (`alignas(32)`, for the cache's lines), and every write
  of it starts at its beginning on a sector of the file: write whole sectors, keep the rest
  for the next write (`EventLog::Flush`). Otherwise the sectors come out shifted, bytes
  repeated and others lost; the twin's card does the same (`test/host/fatfs.h`).
- Large buffers (the loop, the delay, the freezer, the tape stop) live in SDRAM (`DSY_SDRAM_BSS`) and are
  cleared at boot.
- `__attribute__((optimize("-O0")))` and similar per-function overrides are deliberate
  workarounds inherited from the stock firmware. Don't remove them as leftovers.

## License

MIT, see [`LICENSE`](../LICENSE). The effects ported from Bastl Instruments' Kastle 2 FX Wizard
carry their own MIT license in `code/src/LICENSE-kastle2`. [`THIRD_PARTY.md`](../THIRD_PARTY.md)
lists everything FRIZZ builds on.
