# FRIZZ Firmware

Custom firmware for the CHOMPI hardware, forked from WAVE v1.0.

---

## Firmware description

The stereo AUX input goes straight to the outputs through a volume stage taken from TAPE's
Volume Engine. The signal goes to both the headphone and master outputs. The built-in
microphone is not used.

Only the VOLUME knob does anything:

| Control | Function | LED |
|---|---|---|
| Turn (page 1, default) | Output gain, headphone + master (default 75%) | VU meter, scaled by gain |
| Press, then turn (page 2) | Input gain, AUX (default 75%) | blue (0%) to red (100%) |
| SHIFT + turn | Master compressor amount (default off) | dark to light blue |
| Press and hold 1.25 s | Battery check | white full / green / yellow / red |

SHIFT means holding the CHOMPI key with the mode switch DOWN. The CHOMPI key lights white
while it acts as SHIFT. Holding the VOLUME knob at power-on still enters the hardware self-test.

### Where things are in `code/src`

```
chompi_main.cpp        entry point: audio callback, main loop, boot sequence
passthroughEngine.h    the engine: input gain, output gain, master compressor
NormalPage.h           the VOLUME knob and its LED
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

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10. Newer compilers can technically build it,
but the results can sometimes intermittently cause the SD card communication to break. We've
found this compiler to work best.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP, coreJSON (MIT)
code/Chompi_Bootloader/   the bootloader this firmware is loaded by
code/bms_test/            standalone battery-management bring-up example
bin/                      bootloader binary and install script
```

## SD card

The firmware doesn't read anything from the card. It only needs `FRIZZ.bin` on the card
for the bootloader to install it (delete any other `.bin` first), and the hardware self-test
writes and deletes a test file.

## Support Guidelines

This is a discontinuation open-source release. As such, this repo is intended to be a permanent
source for files and documentation, and will likely not be receiving updates in the future. If you wish
to customize your own project, we recommend cloning this repo into your own GitHub.

## Community

Even though this version of CHOMPI is now discontinued, the CLUB is expanding. If you want to
discuss this project, share your creations, see what other users have made on their CHOMPI, feel
free to check out the CHOMPI Open Source channel on the Chase Bliss Discord.

## License

MIT — see [`LICENSE`](../../LICENSE) at the root of this repo. [`THIRD_PARTY.md`](../../THIRD_PARTY.md)
lists the work this builds on. The CHOMPI name and marks are not covered by the license — see
[`TRADEMARKS.md`](../../TRADEMARKS.md).
