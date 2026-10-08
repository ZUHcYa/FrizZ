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
| `README.md`, `INSTALL.md`, `QUICKSTART.md`, `MANUAL.md`, `CHANGELOG.md` | what FRIZZ is, install, quick guide, full controls, what changed since the last release | users |
| `firmware/` | FRIZZ source (`code/`, `bin/`, `test/`, `twin/`); `firmware/README.md` is the developer guide | developers |
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

## Git workflow: branches first, main only after testing

Never commit development work to `main`. Start every change (feature, fix, refactor, docs) on
its own branch off `main`, named for what it does (e.g. `loop-length`, `fix-crusher-level`),
and commit and push there. Merge into `main` only after the user has tested the branch (on
hardware where it touches the firmware) and said so; passing `firmware/test/` or a clean build
is not that approval. If you find yourself on `main` with changes to make, create the branch
before the first commit.

Keep **one untested firmware branch at a time**: test it, merge it, and start the next one off
the new `main`. A branch still open when another reaches `main` merges `main` in, so it's
tested against what's there. Stack a branch on an unmerged one only when it really builds on
it (or would conflict heavily without it), and say so in its PR; the top branch's build is
then the test build for the whole stack, its PRs merge bottom-up once it passes, and a fix
goes on the branch it belongs to, merged upwards. Don't start a third level: get the stack
tested and merged first.

The branch always carries a built firmware for the user to test: every commit that changes
`firmware/code/` rebuilds with `make` in `firmware/code/src` (GCC 10.3, see below) and
includes the fresh `build/FRIZZ.bin` copied to `firmware/bin/FRIZZ.bin`, in the same commit,
so the binary always matches its source. On a merge conflict over it, rebuild rather than pick a
side. It reaches `main` with the merge, so `main` holds the last tested build; releases for
users stay on GitHub's Releases page.

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
  The user ticks the list while testing; merging the PR is the approval to reach `main`. A
  branch built on another unmerged branch either gets a PR covering both or a stacked PR
  based on that branch. Merge an outside contributor's commit unchanged (no squash, rebase or
  cherry-pick) so GitHub marks their PR merged and credits them in the release notes.

A build handed out for testing goes on GitHub as a **pre-release**, never as Latest, so v0.10
users aren't offered it:

- Each test round gets a numbered one, `v<next>-beta.N` (now `v0.11-beta.N`), tagged on the
  branch's pushed head, with that commit's `firmware/bin/FRIZZ.bin` attached and the notes
  taken from `CHANGELOG.md`'s Unreleased section plus a link to the branch's PR. The number
  never moves, so feedback can name the build.
- The pre-release **`beta`** always carries the newest numbered one, and moves only with a
  new one, never with a plain push, at a fixed link
  (`https://github.com/ZUHcYa/FrizZ/releases/download/beta/FRIZZ.bin`). Moving it: `git tag
  -f beta <commit> && git push -f origin beta`, then `gh release upload beta
  firmware/bin/FRIZZ.bin --clobber` and `gh release edit beta` with the new title and notes
  naming the numbered build.
- Cut a new one only when the user asks for a test build; a release for everyone stays a
  normal release on `main`.

## Orientation: ~14.6k files, ~200 of them are source

Of about 14,600 tracked files, about 14,100 are vendored third-party code. Hand-written
`.c/.cpp/.h/.lds` source is about 200 files; most of the rest is factory card audio (211
`.wav`), EAGLE/fabrication files, READMEs, and committed build artifacts. The source lives in exactly two kinds of place:

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
`.bashrc`; prepend its `bin/` explicitly there:
`PATH=~/opt/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH make`. 13.3.rel1 is **not** installed, so TEMPO and the
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

- `code/bms_test/` (stock firmwares only) — a standalone battery-management bring-up example,
  built separately.
- `src/TestPage.h` (stock firmwares only; FRIZZ removed it) — an on-device hardware self-test
  mode (entered by holding encoder 6's switch at power-on).
- `libs/libDaisy/tests/` — vendored.

The only mechanical verification available off-device is: does it compile, does it fit in SRAM, and
for the bootloader, does the md5 match. Verify changes by building; real validation requires
hardware.

The exception is FRIZZ: `firmware/test/` compiles its audio engine on the host and runs a
script of key presses and knob turns through it (3 s per effect plus four combined segments).
`./all.sh` runs everything. `./check.sh` compares HEAD with the working tree; a refactor must
come out `bit-identical`. `./unit.sh NAME` runs one unit check, `NAME.cpp`: `pitch`, `tape`,
`scenes`, `store`, `clicks`, `delay`, `controls`, `keys`, `looper`, `tempo`, `comp`, `level`, `sleep`, `ui`; a new check is just a new
`.cpp`. What each covers is in `firmware/test/README.md`.

`firmware/twin/` is the **virtual CHOMPI**: the whole firmware (`chompi_main.cpp` down, with
libDaisy's UI, Switch, 4021 and MIDI code) compiled unchanged for the host on a simulated
board (the 4021 chains, encoders, WS2812 DMA, charger, card, audio, MIDI in), deterministic
and about 20x real time. `./run.sh -o out.wav -l - SCRIPT` plays a script of keys, knobs,
MIDI and audio into it from power-on and writes the master out and the LEDs; use it to see
what a change does to the play page before the user flashes it. `unit.sh ui` checks the play
page through it. It can't show the CPU load, the codec or anything else about the chip;
`firmware/twin/README.md` has the details.

## FRIZZ's audio callback is CPU-bound

FRIZZ's audio callback has 0.5 ms per 24-sample block. Until effects that are off stopped
processing (`FxGate::Asleep`), a playing loop with the delay ran it at 90-100%. At that
margin, a change that only shifts the memory layout (b5c658c, removing the randomizer) was
enough to make it crackle on the device, while the host harness stayed bit-identical. The
host can't measure this. On the device, wrap `AudioCallback()` in libDaisy's `CpuLoadMeter`
(`OnBlockStart`/`OnBlockEnd`, `Init(sample rate, 24)`) and show `GetMaxCpuLoad()` on a free
key's LED, resetting it every 0.5 s. Many inserts on at once still cost as much as ever.
Keep effects that are off cheap, and suspect the CPU when crackles appear on hardware that
the harness can't reproduce.

## SRAM is the binding constraint, especially on TAPE

TAPE has **376 bytes of SRAM left**. Any addition to TAPE will likely fail at link unless something
is removed first. Every `make` prints a memory usage table — read it. WAVE is the smallest firmware
with the most headroom, which is why its README nominates it as the base for custom firmware.

## Deployment: BOOT_SRAM via SD card

All three firmwares are `APP_TYPE=BOOT_SRAM`: the firmware runs from SRAM, loaded by CHOMPI's own
bootloader out of QSPI flash. Standard Daisy flashing advice does not apply.

- **Normal path:** copy `build/FRIZZ.bin` (stock: `build/CHOMPI.bin`) onto the microSD card
  (delete any other `.bin` first)
  and power on. A slow rainbow LED pattern means it is reprogramming QSPI.
- **Do not use `make program-boot`.** Only WAVE's and FRIZZ's Makefiles override `BOOT_BIN` to
  CHOMPI's bootloader; from TAPE's or TEMPO's it would install libDaisy's generic Daisy bootloader.
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
NoSDPage or MenuPage, and no MIDI out; it does take MIDI clock in (`MidiClock.h` →
`TempoClock.h`, with `TapTempo.h` as the fallback). It reads the card once at boot and writes it
only from `MainLoop()` when an FX scene is saved, copied or deleted, or when the master
compressor's knobs or the mono input setting have rested 2 s (`SceneStore.h`,
`MasterSettings.h`). Its files live in
`/FRIZZ`, which `EnterFrizzDir()` creates at boot on a card without it. Its play page is
`NormalPage.h`; its engine is `passthroughEngine.h` → `Looper.h` + `FxMorph.h` → `FxChain.h` →
`MasterComp.h` → output gain → `limiter.h`.

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

FRIZZ needs only `FRIZZ.bin` at the root. Its state is two text files in `/FRIZZ`:
`frizz_scenes.txt` (FX scenes, `FxScenes.h` format) and `frizz_master.txt` (master settings,
`MasterSettings.h`). The rest of this section is about the stock firmwares.

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
