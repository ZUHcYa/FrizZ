# FRIZZ

**FRIZZ** is an alternative firmware for the CHOMPI. It turns the CHOMPI into a stereo effects
box and looper for whatever you plug into its AUX input.

- **A looper:** free-length loops, or loops locked to whole bars of an incoming MIDI clock.
  Varispeed in fifths and octaves, reverse and scrub on the big transport knob.
- **Ten punch-in effects on the white keys:** freezer, pitch shifter, wavefolder, bitcrusher, DJ
  filter, flanger, resonator, slicer, tempo-synced delay and reverb. Hold a key to play an effect,
  or SHIFT + key to latch it. Knobs 1-4 shape the last effect you touched.
- **An input/loop mix** between the live input and the loop, plus input gain, output gain and a
  master compressor, all on the VOLUME knob.
- **MIDI clock** over TRS or USB, for quantized loops and the tempo-synced effects.

FRIZZ is installed from the SD card like any CHOMPI firmware. It doesn't touch the bootloader,
and you can go back to the stock firmware the same way.

## Get started

| | |
|---|---|
| [**Install**](INSTALL.md) | Put FRIZZ on your CHOMPI, and go back to stock firmware |
| [**Quick guide**](QUICKSTART.md) | Your first loop and your first effects in ten minutes |
| [**Manual**](MANUAL.md) | Every control, every effect, every knob |

## For developers

| | |
|---|---|
| [`firmware/`](firmware/) | The FRIZZ source. Its [README](firmware/README.md) covers the toolchain, the build, the host test harness and debugging |
| [`docs/`](docs/) | Design notes: the looper spec and an overview of the effects across the CHOMPI firmwares |
| [`reference/`](reference/) | The original CHOMPI open-source release, kept unchanged: the stock TAPE, TEMPO and WAVE firmwares, the factory SD cards, the bootloader and the hardware files |

## Credits and license

FRIZZ builds on the [CHOMPI open-source release](https://github.com/CHOMPI-Club/CHOMPI) by
CHOMPI Club and was forked from its WAVE firmware. It runs on Electrosmith's Daisy platform
(libDaisy, DaisySP), and several effects are ported from Bastl Instruments' Kastle 2 FX
Wizard. [`THIRD_PARTY.md`](THIRD_PARTY.md) has the full list.

FRIZZ is an independent project, not an official CHOMPI Club release. The code is **MIT**, see
[`LICENSE`](LICENSE). The CHOMPI name and marks are not covered by that license, see
[`TRADEMARKS.md`](TRADEMARKS.md).
