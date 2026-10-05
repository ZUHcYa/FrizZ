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

Every turn moves 1% per detent. Pressing again on page 3 returns to page 1. The mix starts
fully dry, jumps to fully wet when a recording finishes and back to fully dry when the loop is
erased.

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
printed into a loop.

- **Inserts** (freezer, slicer, flanger, shifter, filter, crusher): replace the signal while
  on and stop the moment they're off. They run in that order, so the filter and crusher work
  on the repeats, slices and modulation.
- **Sends** (delay, reverb): the key opens the effect's input, and its output is added to the
  signal, so tails ring out after the key is released. The two sends run in parallel, both fed
  from the crusher's output.
- **Resonator** (KEY_7): a comb feedback loop around the inserts. While on, it taps the
  signal after the crusher and feeds it back in after the freezer, so it rings through every
  insert that's on.

| Control | Function |
|---|---|
| Hold an FX key | Effect on while held |
| SHIFT + FX key | Latch on / off; a latched effect stays on after release |
| FX key on a latched effect | Clears the latch; the effect stays on until the key is released |
| Knobs 1-4 | The parameters of the most recently pressed FX key, 1% per detent; stepped ones (filter LFO and delay divisions, freezer length, slicer pattern and stereo) move one step per 3 detents |
| Press knobs 1-4 | Resets that parameter to its default |

The FX keys are always dimly lit in their effect's colour, a little brighter for the effect
the knobs edit. While an effect is on, its key follows the audio coming out of it, from
half to full brightness; the delay and reverb keys follow their returns, so they also glow
with the tail after release. The knob LEDs show the parameter values in the effect's
colours; a knob the effect doesn't use is dark and does nothing. Values reset at power-off.

| Key | Effect | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|---|
| 1st white | Filter: the DJ filter from TAPE, TEMPO and WAVE (WAVE's copy) | Cutoff: lowpass left of centre, highpass right, flat at centre (default 30%, lowpass) | Resonance (default 50%) | LFO depth (default off) | LFO division: 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars (default 1 bar) |
| 2nd white | Crusher: TEMPO's sample-rate reducer plus bit reduction | Rate, 21.6 kHz down to 480 Hz (default 60%) | Bits, 16 down to 2 (default 50%, 9 bits) | Tone, lowpass 200 Hz to open (default open) | XOR: flips bits of every sample for a digital buzz, from Kastle 2 FX Wizard's crusher (default off) |
| 3rd white | Freezer: Kastle 2 FX Wizard's, as a beat repeat | Length: 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1 bar (default 1/8) | Feedback: the input overdubbed into the repeats (default 0, pure repeat) | Stereo: the left loop up to 45 ms longer (default off) | Pitch: short pitched loops, 50 Hz up to 290 Hz, replacing the length (default off) |
| 4th white | Slicer: Kastle 2 FX Wizard's rhythmic gate | Pattern, 8 steps of 16ths: `x.......`, `x...x...`, `..x...x.`, `x....x..`, `x..x..x.`, `x.x.x.x.`, `x.x.xx..`, `xxxxxxxx` (default `x..x..x.`) | Decay, 10 ms to 1 s (default 100 ms) | Chance: each step flipped at random, up to 90% (default off) | Stereo: the left channel plays a pattern up the list, the right one down, 0-7 apart (default off) |
| 5th white | Flanger: Kastle 2 FX Wizard's | Rate, 0.02 Hz to 50 Hz (default 0.55 Hz) | Amount: sweep depth and mix together, the top is pure vibrato (default 50%) | Feedback, up to 85% (default 50%) | Stereo: the right LFO runs free and detuned (default off) |
| 6th white | Shifter: Kastle 2 FX Wizard's delay-line pitch shifter | Shift: right of centre up, left down, centre off; further out shifts more, then buzzes (default 70%, about +6 semitones) | Swoop: on the key press, the shift jumps and falls back over 1 s (default off) | Feedback: the shifted sound spirals (default off) | Stereo: the right channel up to 20 Hz faster (default off) |
| 7th white | Resonator: the comb Kastle 2 FX Wizard runs around every mode | Pitch, 22 Hz to 880 Hz (default 110 Hz) | Feedback, up to 98% (default 70%) | Tone: the loop's lowpass, 1 kHz to 15 kHz (default 6.6 kHz) | Stereo: the right channel up to 12 semitones higher (default off) |
| 2nd-to-last white | Delay: TEMPO's tempo-synced delay | Division: 1/8, 1/4T, 1/4, 1/2T, 1/4., 1/2, 1/2., 1 bar, 2 bars (default 1/4) | Feedback (default 40%) | Random: left of centre retrigger / reverse / pitch events, right octave-up shimmer with random pan, centre off (default off) | Level (default 70%) |
| Last white | Reverb (TEMPO's / WAVE's) | Decay (default 60%) | Tone, dark to open (default 60%) | Diffusion (default 60%) | Level (default 70%) |

Filter details:
- **LFO:** a triangle on the cutoff, like WAVE's filter LFO but synced to the same tempo as
  the delay. At full depth it sweeps half the cutoff knob either way, so from the centre it
  goes all the way from lowpass to highpass. It's at the centre of the cutoff on the beat and
  rises towards highpass first. Like the delay's events, the beat is counted from when the
  clock locked, not from the DAW's beat 1.
- LEDs: the key is pink; the knobs go pink (0%) through white to light blue (100%).

Freezer details:
- **Capture:** pressing the key waits for the next 16th, then records. The first pass is the
  live signal, so there's no gap; after one length it repeats. Releasing the key goes back to
  the live signal. It keeps recording past the loop (up to 5 s), so the length can be turned
  up while repeating; turned past what's recorded, it plays on through the recording until
  the length is reached.
- **Feedback:** at 0 the loop repeats unchanged. Turning up mixes the input into it (up to
  30% at 75%, 80% at the top, where the loop also fades by 10% per pass).
- **Pitch:** above 0 the loop is a short pitched one (Kastle's upper half of TIME), 50 Hz
  at 1% up to 290 Hz at 100%.
- Lengths follow the delay's tempo. The loop seam has a 5 ms crossfade (shorter on pitched
  loops), which Kastle doesn't have.
- LEDs: the key is purple; the knobs go purple through white to light blue.

Slicer details:
- **Steps** are 16ths, counted from the clock like the filter LFO, so a pattern is half a
  bar. Each step that's on retriggers a 10 ms attack and the decay. Pressing the key also
  triggers it, so the signal doesn't drop out until the next step.
- LEDs: the key is yellow; the knobs go yellow through white to green.

Flanger details:
- A ~12 ms delay swept up to its full length either way by a triangle LFO. Like Kastle's
  Amount, one knob sets both the depth and the mix: around the middle it flanges and
  chorus-sweeps, at the top it's all delayed signal, a vibrato.
- Pressing the key restarts the sweep from the centre (Kastle's trigger input).
- **Feedback** goes through the swept delay, a classic flanger's resonance. Kastle's FEEDBACK
  is a short comb around every mode, at most 8% on the flanger, so this one is stronger.
- LEDs: the key is light blue; the knobs go light blue through white to purple.

Shifter details:
- An LFO sweeps one delay tap across 11.6 ms as a ramp, fading out and in around each wrap.
  Near the centre that's a slight detune, further out a pitch shift of several semitones,
  and towards the ends the fades turn it into a buzzing, ring-mod-like tone (up to 100 Hz
  down, 260 Hz up, as on Kastle).
- **Swoop** is Kastle's trigger envelope on the sweep rate: 0.1 s up, 1 s back, up to 8x
  down and 20x up. Kastle tied its depth to TIME; here it has its own knob.
- **Feedback** sends the shifted output back into the delay, so each pass shifts again
  (Kastle: its comb around every mode).
- LEDs: the key is red; the shift knob goes blue (down) through white (off) to red (up).

Crusher details:
- **Dive:** every press of the key drops the rate up to 10x over 0.1 s and lets it recover
  over 0.4 s, Kastle's trigger dive.
- **XOR** flips fixed bits of each sample as 16-bit (Kastle's constants, up to 4000), a buzz
  that's loudest where the signal crosses zero. On its own XOR would turn silence into a
  constant offset, so what it adds is DC-blocked.

Resonator details:
- The loop holds a soft clipper, a lowpass (Tone) and a 50 Hz highpass, as on Kastle, so it
  saturates rather than runs away, whatever is inside it. On its own it's a comb on the dry
  sound; with other inserts on, the ringing goes through them on every trip: through the
  shifter it spirals, through the slicer it rings in bursts, through the crusher it turns
  grainy. With the freezer on, it rings the repeats.
- Like Kastle, it turns the input down as feedback goes up (by half at the most). Kastle's
  comb is fixed per mode, 22-440 Hz with about 40% feedback at most; this one is tunable
  and stronger.
- LEDs: the key is white; the knobs go orange through white to light blue.

Delay details:
- **Tempo:** follows MIDI clock, rounded to whole BPM. Without clock it keeps the last tempo
  (120 BPM until a clock arrives). Limited to 50-300 BPM so 2 bars fit the 10 s buffer.
- **Random events** are rolled on every 8th note; the knob's distance from centre is the
  chance. (TEMPO rolled them on its arpeggiator's step instead.)
- **Beat phase:** only the clock's tempo is used, not MIDI Start / Song Position, so the 8th
  notes that random events follow are counted from when the clock locked
  (or from power-on without clock), not from the DAW's beat 1. Echo spacing is unaffected.
- LEDs: division green (short) through white to blue (long); random green (events) through
  white to blue (shimmer).

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
FxChain.h              the punch-in effects in their processing order, with a level meter each
FxSlots.h              which key, LED, colours, defaults and knob steps go with each effect
FxCommon.h             what the effects share: the key's fade, smoothed settings, the base class
Fx*.h                  one effect each: Filter, Crusher, Freezer, Slicer, Flanger, Shifter,
                       Resonator, Delay, Reverb
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
