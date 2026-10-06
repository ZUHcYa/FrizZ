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
code/Chompi_Bootloader/   source of the v6.2 bootloader that loads FRIZZ
code/bms_test/            standalone battery-management bring-up example
bin/                      the v6.2 bootloader binary and its install script
test/                     host-side engine harness and shifter pitch check
```

Design notes live in [`../docs/`](../docs/): the looper spec (`LOOPER.md`) and an overview of
the effects across the CHOMPI firmwares (`FX_OVERVIEW.md`).

## 1. Toolchain

FRIZZ needs **GNU Arm Embedded 10.3-2021.10** (GCC 10.3.1), the Daisy toolchain default. Newer
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

You also need `make`. `dfu-util` is optional (only for reinstalling the bootloader), and the
test harness needs a host `g++` and `python3`.

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

```bash
cd firmware/test
./check.sh        # engine at HEAD vs the working tree: a refactor must print "bit-identical"
./pitch.sh        # the shifter lands on every interval from -12 to +12 semitones
./scenes.sh       # the FX scene file round-trips, and the recall's fast slew ends
```

These checks cover the audio engine only. They don't cover the play page, the looper's
recording, MIDI or the hardware. See [`test/README.md`](test/README.md).

## 4. Put it on the CHOMPI

Copy `build/FRIZZ.bin` to the SD card and power on, as described in
[`INSTALL.md`](../INSTALL.md). FRIZZ is a `BOOT_SRAM` app: CHOMPI's bootloader copies it from
the card into QSPI flash and runs it from SRAM. Standard Daisy flashing advice doesn't apply.

Every CHOMPI already has the bootloader, and an SD update never touches it. Only a blank or
erased Daisy Seed needs it installed. To do that, hold BOOT and tap RESET to enter DFU mode,
then run `bin/install_bootloader.sh`.

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
passthroughEngine.h    the engine: input gain, input/loop mix ("dry/wet" in the code), punch-in FX, output gain, master compressor
FxChain.h              the punch-in effects in their processing order, with a level meter each
FxSlots.h              which key, LED, colours, defaults and knob steps go with each effect
FxCommon.h             what the effects share: the key's fade, smoothed settings, the base class
Fx*.h                  one effect each: Freezer, Shifter, Folder, Crusher, Filter, Flanger,
                       Resonator, Slicer, Delay, Reverb
LICENSE-kastle2        the MIT license of the effects ported from Bastl's Kastle 2 FX Wizard
LedColors.h            the LED colours
DJFilter.h, BasicMMF.h WAVE's DJ filter
granularDelay.h        TEMPO's tempo-synced delay (SimpleCrossfade.h: its crossfades)
reverb.h, fx_engine.h  TEMPO's reverb
TempoClock.h           the tempo and the shared 12 PPQN pulse position for the clocked effects, from MIDI clock or internal
Looper.h               the looper: recording, quantized end, playback, speed, scrub
MidiClock.h            MIDI clock input over TRS and USB
NormalPage.h           the controls (VOLUME, PLAY/LOOP, transport, FX keys and knobs) and their LEDs
ui.h                   page plumbing: events, page switching
limiter.h, EnvFollower.h
                       compressor and VU meter blocks
hardware.h             the CHOMPI hardware: encoders, keys, switches, LEDs, battery
encoder.h / .cpp       encoder driver
temp_led_stuff.h       LED driver (ui_utils.h: LED flush/clear helpers)
BootPage.h, RainbowWavePage.h, TestPage.h
                       boot animation, rainbow-wave animation, hardware test mode
chompi_sram.lds        linker script (the firmware runs from SRAM, placed there by the bootloader)
```

## Rules the code follows

- **No file I/O and no blocking calls in the audio callback.** FRIZZ reads the SD card once at
  boot (the FX scenes) and writes it only from `MainLoop`, when a scene is saved, copied or
  deleted (`SceneStore.h`). The self-test writes to it too.
- Large buffers (the loop, the delay, the freezer) live in SDRAM (`DSY_SDRAM_BSS`) and are
  cleared at boot.
- `__attribute__((optimize("-O0")))` and similar per-function overrides are deliberate
  workarounds inherited from the stock firmware. Don't remove them as leftovers.

## License

MIT, see [`LICENSE`](../LICENSE). The effects ported from Bastl Instruments' Kastle 2 FX Wizard
carry their own MIT license in `code/src/LICENSE-kastle2`. [`THIRD_PARTY.md`](../THIRD_PARTY.md)
lists everything FRIZZ builds on.
