# FRIZZ Firmware

Custom firmware for the CHOMPI hardware, forked from WAVE v1.0.

---

## Firmware description

The stereo AUX input goes to the headphone and master outputs through a volume stage taken
from TAPE's Volume Engine, with a looper on the wet side of a dry/wet mix. The looper can
record free-length loops or loops quantized to whole bars of an incoming MIDI clock. The
white keys punch in effects on the mixed signal. The built-in microphone is not used. The full looper spec is in [`LOOPER.md`](LOOPER.md).

### VOLUME knob

| Control | Function | LED |
|---|---|---|
| Turn (page 1, default) | Output gain, headphone + master (default 75%) | VU meter, scaled by gain |
| Press, then turn (page 2) | Input gain, AUX (default 75%) | blue (0%) to red (100%) |
| Press again, then turn (page 3) | Master compressor amount (default off) | dark to light blue |
| SHIFT + turn | Dry/wet mix: dry = input only, wet = looper only | green (dry) to purple (wet) |
| Press and hold 1.25 s | Battery check | white full / green / yellow / red |

Pressing again on page 3 returns to page 1. The mix starts fully dry, jumps to fully wet when a
recording finishes and back to fully dry when the loop is erased.

### Looper

| Looper | Key | Result |
|---|---|---|
| Empty | LOOP | Start recording |
| Empty | hold PLAY, press LOOP | Start a **quantized** recording (needs MIDI clock, otherwise LOOP blinks red 3 times) |
| Recording | LOOP | Stop now, or for a quantized recording at the end of the current bar, then play |
| Loop exists | PLAY | Play / pause |
| Loop exists | hold PLAY + LOOP 2 s | Erase |

- **Quantized:** recording starts on the press, which counts as bar 1 (4/4). Ending it records
  to the end of the bar in progress, so the loop is always a whole number of bars. It closes
  immediately if the clock stops.
- **Length:** up to 2:45. The loop lives in RAM and is gone at power-off. There's no overdub.

Transport knob (the big purple one), once a loop exists:

| Control | Function |
|---|---|
| Turn while playing | Speed in 5ths and octaves, 2× down to 1/16×, then reverse back up to −2× (4 detents per step) |
| Turn while paused | Scrub |
| Press | Back to 1× forward |

LEDs: LOOP is red while recording and blinks while a quantized recording finishes its bar.
While a loop plays, PLAY and LOOP crossfade in white to show the position (dimmed when
paused). The transport LEDs show speed and direction.

### Punch-in FX

Effects sit on the white keys and act on the whole mix, after the dry/wet knob and before the
output gain and the master compressor. The looper records the dry input, so an effect is never
printed into a loop. So far only the first (lowest) white key has an effect.

| Control | Function |
|---|---|
| Hold an FX key | Effect on while held |
| SHIFT + FX key | Latch on / off; a latched effect stays on after release |
| FX key on a latched effect | Clears the latch; the effect stays on until the key is released |
| Knobs 1-4 | The 4 parameters of the most recently pressed FX key |

The FX key is dim while the knobs edit its effect and lit while it is on. The knob LEDs show
the parameter values, yellow (0) through orange to red (1). Values reset at power-off.

| Key | Effect | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|---|
| 1st white | Crusher: TEMPO's sample-rate reducer plus bit reduction | Rate, 21.6 kHz down to 480 Hz (default 60%) | Bits, 16 down to 2 (default 50%, 9 bits) | Tone, lowpass 200 Hz to open (default open) | Mix (default 100%) |

### MIDI clock

Quantized recording follows MIDI clock (24 PPQN) from the TRS MIDI input or USB. CHOMPI is a
USB device, so USB clock comes from a computer or a host. Whichever source ticks first is
used, until it has been silent for 0.5 s. Only clock is read; there's no MIDI out.

SHIFT means holding the CHOMPI key, with the mode switch in either position. The CHOMPI key
lights white while it acts as SHIFT. Holding the VOLUME knob at power-on still enters the
hardware self-test.

### Where things are in `code/src`

```
chompi_main.cpp        entry point: audio callback, main loop, boot sequence
passthroughEngine.h    the engine: input gain, dry/wet mix, punch-in FX, output gain, master compressor
PunchFx.h              the punch-in effects (crusher)
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

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10. Newer compilers can technically build it,
but the results can sometimes intermittently cause the SD card communication to break. We've
found this compiler to work best.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP (MIT)
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
