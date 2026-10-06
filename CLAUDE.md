# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

**FRIZZ**, a custom firmware for **CHOMPI** (a discontinued chromatic sampler / tape instrument by
CHOMPI Club / Chase Bliss), forked from CHOMPI's WAVE firmware. Bare-metal C++ for a Daisy Seed2
DFM (STM32H750). It is built on top of CHOMPI's archival open-source release (`upstream` remote),
which will not receive updates.

## Repo layout: user-facing at the root, everything else below it

| Path | What | Audience |
|---|---|---|
| `README.md`, `INSTALL.md`, `QUICKSTART.md`, `MANUAL.md` | what FRIZZ is, install, quick guide, full controls | users |
| `firmware/` | FRIZZ source (`code/`, `bin/`, `test/`); `firmware/README.md` is the developer guide | developers |
| `docs/` | our design notes: `LOOPER.md` (looper spec), `FX_OVERVIEW.md` | developers |
| `reference/` | the original CHOMPI release, unchanged except for links: `reference/firmware/{chompi-tape,chompi-tempo,chompi-wave,chompi-bootloader-v6.4-beta,card-profiles}`, `reference/hardware/`, CHOMPI's README | reference only |
| `LICENSE`, `THIRD_PARTY.md`, `TRADEMARKS.md`, `CLAUDE.md` | legal, this file | — |

Keep the root user-facing: anything that only helps development goes in `docs/` or
`firmware/`. When a control changes, update `MANUAL.md` (and `QUICKSTART.md` if it's covered
there). FRIZZ was moved from `firmware/frizz/` to `firmware/`; commits before that use the old
path (`firmware/test/run.sh` handles both).

The panel artwork and CHOMPI logos are deliberately absent for copyright reasons, and the CHOMPI
name/marks are excluded from the MIT license (`TRADEMARKS.md`). Don't reintroduce branding into
derived hardware files.

## Orientation: 12.8k files, ~140 of them are source

Of 12,821 tracked files, 12,371 are vendored third-party code. Hand-written `.c/.cpp/.h/.lds`
source is 141 files; most of the rest is factory card audio (211 `.wav`), EAGLE/fabrication files,
READMEs, and committed build artifacts. The source lives in exactly two kinds of place:

- `firmware/code/src/` — FRIZZ
- `reference/firmware/{chompi-tape,chompi-tempo,chompi-wave}/code/src/` — ~30-40 files each
- `reference/firmware/chompi-bootloader-v6.4-beta/bootloader/` — plus `shared/` (Electrosmith's v6.4 source)

Everything under `libs/`, `cube_dfu/`, `Drivers/`, `Middlewares/`, and any `build/` directory is
vendored or generated. Exclude those from greps or results are unusable. `.gitattributes` already
marks them `linguist-vendored`.

## The three firmwares are independent forks, not a shared core

TAPE 2.0, TEMPO 1.0 and WAVE 1.0 each ship a full private copy of the source tree and the vendored
libraries, and these have diverged. (The exception is `code/Chompi_Bootloader/` — the shipped v6.2
bootloader source, byte-identical in all three.) They share ~20 filenames (`hardware.h`, `ui.h`, `NormalPage.h`,
`MenuPage.h`, `temp_led_stuff.h`, `encoder.cpp`, `chompi_sram.lds`, `Makefile`, …) and **every one
of those files differs between firmwares**. Consequences:

- A fix in one firmware does not propagate. Changing shared behavior means three edits.
- Every grep hit appears three times. Always scope searches to one firmware folder.
- Never assume a same-named file matches the one you already read.

Card profiles and preset formats are also not interchangeable between firmwares.

## Toolchain — pinned per target, and the pins matter

| Target | Compiler | Why |
|---|---|---|
| TAPE, WAVE, custom firmware | GNU Arm Embedded **10.3-2021.10** (GCC 10.3.1) | Newer GCC intermittently breaks SD-card communication; on TAPE it also overflows SRAM at the link step |
| TEMPO | Arm GNU Toolchain **13.3.rel1** (GCC 13.3.1) | What the released firmware was built with |
| Bootloader v6.4 | Arm GNU Toolchain **13.3.rel1** | Reproduces the released binary byte-for-byte |

Switch by putting the right `bin/` first on `PATH`; verify with `arm-none-eabi-gcc --version`
before building. Do not use Homebrew's `arm-none-eabi-gcc` for the bootloader (compiler only, no
newlib). `firmware/README.md` is FRIZZ's build guide (Linux and macOS);
`reference/firmware/README.md` is CHOMPI's original macOS-only one — its
`/Applications/ArmGNUToolchain/...` paths are examples, not real locations on a Linux box.

On this machine, GNU Arm Embedded 10.3-2021.10 is installed at
`~/opt/gcc-arm-none-eabi-10.3-2021.10/` (ARM's official Linux tarball) and prepended to `PATH` in
`~/.bashrc`, so FRIZZ, TAPE and WAVE build with a plain `make`. Non-interactive shells may not read
`.bashrc`; prepend its `bin/` explicitly there. 13.3.rel1 is **not** installed, so TEMPO and the
bootloader can't be built yet. Homebrew can't supply either version: its `arm-none-eabi-gcc`
formula is GCC 16 without newlib, and the `gcc-arm-embedded` cask is macOS-only.

## Build

Application firmware — run `make` from the firmware's `code/src`, never from the repo root:

```bash
cd firmware/code/src                   # FRIZZ; stock: reference/firmware/chompi-wave/code/src
make              # output: build/FRIZZ.bin (flashable) and build/FRIZZ.elf (gdb); stock firmwares: CHOMPI.*
make clean
make -j4
```

Prebuilt `libdaisy.a`, `libdaisysp.a` and their object files are **committed** under
`code/libs/*/build/`, so rebuilding the libraries is not normally necessary. If you change a
vendored library — or if make decides to rebuild one — do it with `make` in `code/libs/libDaisy` or
`code/libs/DaisySP` using the same compiler as the app.

Bootloader:

```bash
cd reference/firmware/chompi-bootloader-v6.4-beta
CHOMPI_TOOLCHAIN_BIN=/path/to/arm-gnu-toolchain-13.3.rel1/bin ./build-bootloader.sh        # or: ... ./build-bootloader.sh clean
```

The script builds libDaisy then the bootloader and checks the result against the release
(119,612 bytes, md5 `580b187fec405849fb401eb699281e4c`).

## No test suite (except FRIZZ's engine harness)

There is no CI, no test runner, and no Cursor/Copilot rules. The only clang-format tooling is
vendored inside libDaisy/DaisySP's own `ci/`; it does not apply to `code/src/`. The things that
look like tests aren't:

- `code/bms_test/` — a standalone battery-management bring-up example, built separately.
- `src/TestPage.h` — an on-device hardware self-test mode (entered by holding encoder 6's switch at
  power-on).
- `libs/libDaisy/tests/` — vendored.

The only mechanical verification available off-device is: does it compile, does it fit in SRAM, and
for the bootloader, does the md5 match. Verify changes by building; real validation requires
hardware.

The exception is FRIZZ: `firmware/test/` compiles its audio engine on the host and runs a
scripted 39 s of key presses and knob turns through it. `./all.sh` runs every check below.
`./check.sh` compares HEAD with the
working tree; a refactor must come out `bit-identical`. `./pitch.sh` checks the shifter lands on
every interval; `./scenes.sh` checks the FX scene file format; `./controls.sh` checks the play
page's FX and scene logic (`FxControls.h`, `SceneControls.h`); `./keys.sh` its CHOMPI, PLAY and
LOOP keys (`PlayKeys.h`); `./looper.sh` checks the looper
without a clock; `./tempo.sh` checks tap tempo, the FX's tempo locked to the loop and scene morphs landing on
its bar lines (`FxMorph.h`). None
covers the LEDs, `NormalPage.h`'s key routing, real MIDI or the hardware. See its README.

## SRAM is the binding constraint, especially on TAPE

TAPE has **376 bytes of SRAM left**. Any addition to TAPE will likely fail at link unless something
is removed first. Every `make` prints a memory usage table — read it. WAVE is the smallest firmware
with the most headroom, which is why its README nominates it as the base for custom firmware.

## Deployment: BOOT_SRAM via SD card

All three firmwares are `APP_TYPE=BOOT_SRAM`: the firmware runs from SRAM, loaded by CHOMPI's own
bootloader out of QSPI flash. Standard Daisy flashing advice does not apply.

- **Normal path:** copy `build/CHOMPI.bin` onto the microSD card (delete any other `.bin` first)
  and power on. A slow rainbow LED pattern means it is reprogramming QSPI.
- **Do not use `make program-boot`.** Only WAVE's Makefile overrides `BOOT_BIN` to CHOMPI's
  bootloader; from TAPE's or TEMPO's it would install libDaisy's generic Daisy bootloader.
- The bootloader itself lives in internal flash and is never touched by an SD update. Install it
  over DFU at `0x08000000` (`dfu-util -a 0 -s 0x08000000:leave -D ... -d ,0483:df11`) or with an
  ST-Link. `bin/install_bootloader.sh` in each firmware folder does this for the shipped v6.2.
- Debugging needs an STLINK-V3MINIE and a Daisy with a soldered debug header:
  `openocd -f interface/stlink.cfg -f target/stm32h7x.cfg`, then `arm-none-eabi-gdb CHOMPI.elf` and
  `target remote localhost:3333`.

## Firmware architecture

Each firmware follows the same skeleton; `chompi_main.cpp` documents it. Three execution contexts,
by descending priority:

1. **`AudioCallback()`** — the audio ISR, once per ~24-sample block at 48 kHz. Polls controls,
   generates UI events, runs the engine. Four output channels (headphone L/R + master L/R, two SAI
   peripherals in sync) and four inputs (mic, X, aux L/R).
2. **`SDCallback()`** — a lower-priority hardware-timer callback. Drains **one**
   `FileStreamingManager` request per tick and advances chunked preset writes, so FatFs never
   blocks audio.
3. **`MainLoop()`** — UI event dispatch, MIDI out, battery checks, boot sequencing.

The rule this enforces: **no file I/O and no blocking call from the audio ISR.** SD work is posted
as `FileRequest`s to a queue; large writes are chunked across many `SDCallback()` ticks and
committed by writing a temp file then renaming it.

**FRIZZ is simpler:** it builds `FRIZZ.bin`, has no `SDCallback()`, `FileStreamingManager`,
NoSDPage or MenuPage, and no MIDI out. It reads the card once at boot and writes it only from
`MainLoop()` when an FX scene is saved, copied or deleted (`SceneStore.h`). Its files live in
`/FRIZZ`, which `EnterFrizzDir()` creates at boot on a card without it. Its play page is
`NormalPage.h`; its engine is `passthroughEngine.h` → `Looper.h` + `FxMorph.h` → `FxChain.h`.

Supporting layers, same names in all three firmwares (different contents):

- `hardware.h` — the board: 28 keys and 6 encoder switches read through a CD4021 shift-register
  chain (`Hardware::SwId`), encoders, 35 RGB LEDs, MP2722 charger/battery over I²C, USB switch.
  Also brings up the second PCM3060 codec on SAI2. `encoder.{h,cpp}` and `temp_led_stuff.h` /
  `ui_utils.h` are the drivers.
- `ui.h` — built on libDaisy's `daisy::UI` page stack. `GenerateEvents()` turns debounced hardware
  state into `UiEventQueue` events; `DoEvents()` dispatches to the active page. Pages:
  `NormalPage` (play), `MenuPage` (the SHIFT layer, reached by holding the CHOMPI key with the mode
  switch down), `TestPage`, `BootPage`, `NoSDPage`, `RainbowWavePage`. `MenuPage` and `NormalPage`
  are the largest files in each firmware.
- `OptionsManager.h` / `PresetManager.h` — `options.json` and `presets.json` on the card, parsed
  with coreJSON.
- `chompi_sram.lds` — the linker script placing the app in SRAM. Large audio buffers go to SDRAM
  via `DSY_SDRAM_BSS` / `.sdram_bss`, which `ZeroSDRAM()` clears at boot because startup code
  doesn't.

Where they diverge — the engine:

| | Engine files |
|---|---|
| WAVE | `subtractiveEngine.h` (8 wavetable voices → filter → amp env → shared delay/reverb/comp/sat), `WavetableManager.h`, `Sequencer.h`, `clockManager.h`, `MidiManager.h` |
| TAPE | `DSPEngine.h` (the largest hand-written file here, ~1600 lines), `LooperEngine.h`, `Sampler.h`, `SampleReader.h`, `RamBuffer.h`, `FileCopier.h`, `Warble.h` |
| TEMPO | `EngineBase.h` with `SampleEngine.h` (chromatic) and `SliceEngine.h` (16 slices), `ArpeggiatorSequencer.h`, `SampleManager.h`, `StateSaver.h`, `granularDelay*.h` |

Shared DSP blocks (`reverb.h`, `fx_engine.h`, `DJFilter.h`, `BasicMMF.h`, `limiter.h`,
`EnvFollower.h`, `InterpolatedDelayLine.h`) are per-firmware copies too.

## Vendored libraries are patched — never swap in upstream

`code/libs/libDaisy/` is Electrosmith's CHOMPI adaptation of libDaisy v5.4.0 with further local
changes, and **TAPE's copy differs from TEMPO's and WAVE's** (which are identical to each other).
`THIRD_PARTY.md` enumerates the diffs; the load-bearing ones:

- TEMPO/WAVE: `src/per/tim.{h,cpp}` adds TIM16 (TEMPO's clock and WAVE's MIDI clock use it) and
  `src/hid/midi.h` adds `GetUartHandle`/`GetMutableTransport`. **The build fails without these.**
- TEMPO/WAVE: `src/per/uart.cpp` has interrupt-blocking guards removed for tighter MIDI timing.
  Builds fine against pristine upstream but MIDI output timing won't match the release.
- TAPE: `src/hid/midi.h` adds MIDI send helpers the firmware calls (build fails without them);
  `src/per/tim.{h,cpp}` compiles timer init/start at `-O0` with auto-reload preload disabled.
- The bootloader's copy additionally carries an upstream QSPI driver and two `BootInfo` enum
  additions — see `reference/firmware/chompi-bootloader-v6.4-beta/LIBDAISY_PATCH.md`.

Also note `__attribute__((optimize("-O0")))` on `UserInterface::WritePresets()` and similar
per-function optimization overrides — these are deliberate workarounds for timing/audio artifacts,
not leftovers.

## SD card layout

The card is the firmware's filesystem: one `CHOMPI.bin`, the audio assets, `options.json`,
`presets.json`. FAT32, assets at the card root. `reference/firmware/card-profiles/` holds the three factory
cards ready to copy.

Format details that bite:

- **WAVE** loads the first seven `.wav` files alphabetically as wavetables, reading raw 32-bit
  float data from byte offset 136 (Serum layout, 33 waves × 2048 samples) rather than parsing the
  WAV header. Presets reference a wavetable by **slot index, not name** — renaming or reordering
  files silently repoints saved presets.
- **TAPE** samples are `<instrument>_<bank><slot>.wav`, 48 kHz 16-bit stereo, each with a
  `_double` variant used for high-pitched playback.
- **TEMPO** samples are 48 kHz 16-bit stereo in `chromatic/`, `slice/` and `buffer/`, capped at 10
  seconds per slot.

## Hardware files

`reference/hardware/hardware-pcb/` is an EAGLE 9.6.2 project (two boards on one v-scored panel) plus the
September 2023 fabrication package; `reference/hardware/hardware-enclosure/` is six panel `.brd` files with
laser-cutting DXFs. These are binary CAD files — don't attempt text edits. The BOM
(`CHOMPI_Rev4_BOM.csv`) is the authoritative parts list.
