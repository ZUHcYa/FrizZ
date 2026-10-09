# The stock CHOMPI firmwares under `reference/`

Notes for working with CHOMPI's original release (TAPE 2.0, TEMPO 1.0, WAVE 1.0, the v6.4
bootloader, the hardware files) in `reference/`. FRIZZ itself is in `firmware/`; its guide is
[`firmware/README.md`](../firmware/README.md). `reference/firmware/README.md` is CHOMPI's own
macOS build guide: its `/Applications/ArmGNUToolchain/...` paths are examples.

## Where the source is

Of about 14,600 tracked files in the repo, about 14,100 are vendored third-party code; most of
the rest is factory card audio (211 `.wav`), EAGLE/fabrication files and committed build
artifacts. Hand-written source under `reference/`:

- `reference/firmware/{chompi-tape,chompi-tempo,chompi-wave}/code/src/`: ~30-40 files each
- `reference/firmware/chompi-bootloader-v6.4-beta/bootloader/`, plus `shared/` (Electrosmith's v6.4 source)

Everything under `libs/`, `cube_dfu/`, `Drivers/`, `Middlewares/` and any `build/` directory is
vendored or generated; `.gitattributes` marks it `linguist-vendored`.

## Three independent forks, not a shared core

TAPE, TEMPO and WAVE each ship a full private copy of the source tree and the vendored
libraries, and these have diverged. (The exception is `code/Chompi_Bootloader/`, the shipped
v6.2 bootloader source, byte-identical in all three.) They share ~20 filenames (`hardware.h`,
`ui.h`, `NormalPage.h`, `MenuPage.h`, `temp_led_stuff.h`, `encoder.cpp`, `chompi_sram.lds`,
`Makefile`, …) and **every one of those files differs between firmwares**:

- A fix in one firmware does not propagate. Changing shared behaviour means three edits.
- Every grep hit appears three times. Scope searches to one firmware folder.
- Never assume a same-named file matches the one you already read.

Card profiles and preset formats are not interchangeable between firmwares either.

## Toolchain: pinned per target

| Target | Compiler | Why |
|---|---|---|
| TAPE, WAVE (and FRIZZ) | GNU Arm Embedded **10.3-2021.10** (GCC 10.3.1) | Newer GCC intermittently breaks SD-card communication; on TAPE it also overflows SRAM at the link step |
| TEMPO | Arm GNU Toolchain **13.3.rel1** (GCC 13.3.1) | What the released firmware was built with |
| Bootloader v6.4 | Arm GNU Toolchain **13.3.rel1** | Reproduces the released binary byte-for-byte |

Switch by putting the right `bin/` first on `PATH`; check with `arm-none-eabi-gcc --version`.
13.3.rel1 is not installed on the maintainer's machine, so TEMPO and the bootloader can't be
built there yet. Homebrew's `arm-none-eabi-gcc` is GCC 16 without newlib: no use for either.

```bash
cd reference/firmware/chompi-wave/code/src && make      # build/CHOMPI.bin; likewise tape, tempo
cd reference/firmware/chompi-bootloader-v6.4-beta
CHOMPI_TOOLCHAIN_BIN=/path/to/arm-gnu-toolchain-13.3.rel1/bin ./build-bootloader.sh
```

The script builds libDaisy then the bootloader and checks the result against the release
(119,612 bytes, md5 `580b187fec405849fb401eb699281e4c`).

**SRAM is the binding constraint, especially on TAPE**: 376 bytes left. Any addition to TAPE
will likely fail at link unless something is removed first. WAVE has the most headroom, which
is why FRIZZ started from it.

**Don't use `make program-boot`** from TAPE or TEMPO: only WAVE's and FRIZZ's Makefiles point
`BOOT_BIN` at CHOMPI's bootloader; the others would install libDaisy's generic one. The
bootloader lives in internal flash and an SD update never touches it; install it over DFU at
`0x08000000` (`dfu-util -a 0 -s 0x08000000:leave -D ... -d ,0483:df11`) or with an ST-Link
(`bin/install_bootloader.sh` in each firmware folder does this for v6.2).

## Not tests

`code/bms_test/` is a standalone battery-management bring-up example; `src/TestPage.h` is an
on-device hardware self-test (hold encoder 6's switch at power-on); `libs/libDaisy/tests/` is
vendored. Off the device, the only verification is: does it compile, does it fit, and for the
bootloader, does the md5 match.

## Architecture of the stock firmwares

Three execution contexts, by descending priority:

1. `AudioCallback()`: the audio ISR, once per ~24-sample block. Four outputs (headphone L/R +
   master L/R, two SAI peripherals in sync) and four inputs (mic, X, aux L/R).
2. `SDCallback()`: a lower-priority timer callback that drains **one** `FileStreamingManager`
   request per tick and advances chunked preset writes (temp file, then rename), so FatFs never
   blocks audio.
3. `MainLoop()`: UI event dispatch, MIDI out, battery checks, boot sequencing.

Supporting layers, same names in all three firmwares (different contents):

- `hardware.h`: 28 keys and 6 encoder switches through a CD4021 shift-register chain
  (`Hardware::SwId`), encoders, 35 RGB LEDs, MP2722 charger/battery over I²C, USB switch, the
  second PCM3060 codec on SAI2. `encoder.{h,cpp}`, `temp_led_stuff.h` and `ui_utils.h` are the drivers.
- `ui.h`: libDaisy's `daisy::UI` page stack. Pages: `NormalPage` (play), `MenuPage` (the SHIFT
  layer, CHOMPI key held with the mode switch down), `TestPage`, `BootPage`, `NoSDPage`,
  `RainbowWavePage`.
- `OptionsManager.h` / `PresetManager.h`: `options.json` and `presets.json`, parsed with coreJSON.
- `chompi_sram.lds`: places the app in SRAM; large buffers go to SDRAM (`DSY_SDRAM_BSS`),
  cleared by `ZeroSDRAM()` at boot.

The engines:

| | Engine files |
|---|---|
| WAVE | `subtractiveEngine.h` (8 wavetable voices → filter → amp env → shared delay/reverb/comp/sat), `WavetableManager.h`, `Sequencer.h`, `clockManager.h`, `MidiManager.h` |
| TAPE | `DSPEngine.h` (~1600 lines), `LooperEngine.h`, `Sampler.h`, `SampleReader.h`, `RamBuffer.h`, `FileCopier.h`, `Warble.h` |
| TEMPO | `EngineBase.h` with `SampleEngine.h` (chromatic) and `SliceEngine.h` (16 slices), `ArpeggiatorSequencer.h`, `SampleManager.h`, `StateSaver.h`, `granularDelay*.h` |

Shared DSP blocks (`reverb.h`, `fx_engine.h`, `DJFilter.h`, `BasicMMF.h`, `limiter.h`,
`EnvFollower.h`, `InterpolatedDelayLine.h`) are per-firmware copies too.
`__attribute__((optimize("-O0")))` on `UserInterface::WritePresets()` and similar overrides are
deliberate workarounds for timing/audio artifacts.

## Vendored libDaisy: patched, never swap in upstream

`code/libs/libDaisy/` is Electrosmith's CHOMPI adaptation of libDaisy v5.4.0 with further local
changes, and **TAPE's copy differs from TEMPO's and WAVE's** (which are identical).
`THIRD_PARTY.md` lists the diffs; the load-bearing ones:

- TEMPO/WAVE: `src/per/tim.{h,cpp}` adds TIM16 and `src/hid/midi.h` adds
  `GetUartHandle`/`GetMutableTransport`. **The build fails without these.**
- TEMPO/WAVE: `src/per/uart.cpp` has interrupt-blocking guards removed for tighter MIDI timing.
- TAPE: `src/hid/midi.h` adds MIDI send helpers (the build fails without them);
  `src/per/tim.{h,cpp}` compiles timer init/start at `-O0` with auto-reload preload disabled.
- The bootloader's copy also carries an upstream QSPI driver and two `BootInfo` enum additions:
  `reference/firmware/chompi-bootloader-v6.4-beta/LIBDAISY_PATCH.md`.

## SD card

The card is the firmware's filesystem: one `CHOMPI.bin`, the audio assets, `options.json`,
`presets.json`, FAT32, assets at the root. `reference/firmware/card-profiles/` holds the three
factory cards ready to copy.

- **WAVE** loads the first seven `.wav` files alphabetically as wavetables, reading raw 32-bit
  float data from byte offset 136 (Serum layout, 33 waves × 2048 samples) rather than parsing
  the WAV header. Presets reference a wavetable by **slot index, not name**: renaming or
  reordering files silently repoints saved presets.
- **TAPE** samples are `<instrument>_<bank><slot>.wav`, 48 kHz 16-bit stereo, each with a
  `_double` variant used for high-pitched playback.
- **TEMPO** samples are 48 kHz 16-bit stereo in `chromatic/`, `slice/` and `buffer/`, capped at
  10 seconds per slot.

## Hardware files

`reference/hardware/hardware-pcb/` is an EAGLE 9.6.2 project (two boards on one v-scored panel)
plus the September 2023 fabrication package; `reference/hardware/hardware-enclosure/` is six
panel `.brd` files with laser-cutting DXFs. Binary CAD files: don't attempt text edits. The BOM
(`CHOMPI_Rev4_BOM.csv`) is the authoritative parts list. Debugging needs an STLINK-V3MINIE and
a Daisy with a soldered debug header (`firmware/README.md`, *Debug*).
