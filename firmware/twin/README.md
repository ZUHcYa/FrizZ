# The virtual CHOMPI

FRIZZ's own firmware running on a PC: `chompi_main.cpp` and everything under it (the play page,
the engine, the card, MIDI clock), and libDaisy's UI, Switch, 4021 and MIDI code, compiled
unchanged with the host's `g++`. Only the board underneath is simulated (`host/`):

| On the device | In the twin |
|---|---|
| 28 keys and 6 encoder switches through five CD4021s, bit-banged by libDaisy's driver | the same driver clocking a model of the chain: keys debounce exactly as on the device |
| encoders 1-4 on a sixth 4021, 5 and 6 on their own pins | a full quadrature cycle per detent, 8 ms |
| 35 WS2812 LEDs fed by timer PWM + DMA | the DMA's buffer decoded back to the bytes each LED latches |
| MP2722 charger on I²C | a battery with a voltage, a charger plugged or not, shipping mode |
| SAI audio, 24-sample blocks at 48 kHz | the same callback, block by block |
| MIDI in over the TRS jack's UART | bytes fed to libDaisy's parser |
| SD card | `test/host/fatfs.h`, a card in memory |
| the 64 MB SDRAM | mapped at its address, 0xc0000000 |

The firmware's `main()` runs as a coroutine: each 0.5 ms block runs the audio callback, then
`main()` until it has used up the block (its time moves with `System::Delay*` and `GetNow`).
So the boot sequence, the 2 ms UI tick, the battery checks and the card writes run as on the
device, and a run is deterministic: the same inputs give the same samples and LEDs. It runs
13 (every effect on, a loop playing) to 20 times faster than real time.

What it can't show: anything about time on the chip. The CPU load (crackles), the caches and
memory placement, the codec, the card's speed. Those need the device (CLAUDE.md, *FRIZZ's
audio callback is CPU-bound*). Nor races between the audio interrupt and `main()`: here the
callback only comes between blocks, `main()` only gives way in `GetNow` and `System::Delay*`,
the charger's I2C reply arrives at once and each LED chain's DMA finishes once per block.

## Use

```bash
./run.sh -o out.wav -l leds.txt examples/filter.txt   # builds if needed, plays the script
./build.sh                                           # just build: build/frizz-twin, build/libtwin.a
```

`-o` writes the master out (32-bit float stereo WAV), `-l` every change of the LEDs (`-` for
stdout): the time in ms, the 10 panel LEDs (`pth`, in `NormalPage.h`'s numbering: 0 CHOMPI,
1-4 knobs, 5/6 transport, 7 PLAY, 8 LOOP, 9 VOLUME), then the 25 key LEDs (`smt`), each as
`rrggbb` scaled back to full (FRIZZ sends the panel at 1/11 and the keys at 1/4).

`../test/ui.cpp` (`unit.sh ui`) uses the same twin through `twin.h`.

## Scripts

One command per line, `#` starts a comment. The device is switched on at the first command
that needs it; time moves only with `wait` and `at`.

| Command | |
|---|---|
| `wait MS` / `at MS` | run for MS / until MS after power-on |
| `down KEY` / `up KEY` / `tap KEY [MS]` | a key by its `Hardware::SwId` name: `KEY_1` .. `KEY_28` (white keys 1-15 are `KEY_1`-`KEY_15`; CHOMPI, PLAY, LOOP are `KEY_26`-`KEY_28`), `ENC_1_SW` .. `ENC_6_SW`. `tap` holds it 60 ms |
| `turn ENC N` | encoder 1-6 (SW1-SW6: 4 is knob 1, 1-3 knobs 2-4, 5 the transport, 6 VOLUME) by N detents |
| `toggle 0\|1` | the mode switch, as the 4021 reads it |
| `input sine HZ AMP` / `input wav FILE` / `input off` | what goes into AUX (a WAV loops) |
| `midi HEX...` / `clock BPM` | raw bytes into the MIDI jack / a running MIDI clock (`clock 0` stops it) |
| `battery V [plugged] [full]` | the battery's voltage and the charger |
| `card put PATH FILE` / `card remove` / `card insert` / `card dump` | the SD card: put a file on it before power-on (`/FRIZZ/frizz_scenes.txt`), take it out, print it |
| `leds` | print the LEDs now |
| `expect led pth\|smt N RRGGBB` / `expect on` / `expect off` | fail (exit 1) unless so |

## Files

- `twin.h` / `twin.cpp`: the API and the simulated board; `twin.cpp` includes `chompi_main.cpp`.
- `host/`: what replaces libDaisy's hardware layer (`daisy.h`, `daisy_seed.h`, `per/`, `sys/`, ...).
- `cli.cpp`: `frizz-twin`, the script player.
- `build.sh`: copies the firmware, the libDaisy files it uses and `host/` into `build/tree` and
  builds there, so every include resolves to either the real file or its stand-in.
