# FRIZZ Manual

Every control in FRIZZ. For a first session, start with the [quick guide](QUICKSTART.md).

**SHIFT** means holding the CHOMPI key, with the mode switch in either position. The CHOMPI key
lights white while it acts as SHIFT.

## Overview

The stereo AUX input goes to the headphone and master outputs through a volume stage taken
from TAPE's Volume Engine, with a looper on the wet side of a dry/wet mix. The looper can
record free-length loops or loops quantized to whole bars of an incoming MIDI clock. The
white keys punch in effects on the mixed signal, and four dark keys save and recall their
settings as scenes. The built-in microphone is not used.

## VOLUME knob

| Control | Function | LED |
|---|---|---|
| Turn (page 1, default) | Output gain, headphone + master (default 75%) | VU meter, scaled by gain |
| Press, then turn (page 2) | Input gain, AUX (default 75%) | blue (0%) to red (100%) |
| Press again, then turn (page 3) | Master compressor amount (default off) | dark to light blue |
| SHIFT + turn | Input/loop mix: input only to looper only | green (input) to purple (loop) |
| Press and hold 1.25 s | Battery check | white full / green / yellow / red |

Every turn moves 1% per detent. Pressing again on page 3 returns to page 1. The mix starts
on the input only, jumps to the loop only when a recording finishes and back to the input only
when the loop is erased. The punch-in FX come after this mix, so they act on the input as well
as the loop.

## Looper

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

## Punch-in FX

Effects sit on the white keys and act on the whole mix, after the input/loop mix and before the
output gain and the master compressor. The looper records the input before the effects, so an effect is never
printed into a loop.

The keys run in signal order, left to right:

```
freezer -> shifter -> folder -> crusher -> filter -> flanger -> slicer -> delay -> reverb
           |<------------------- resonator loop ------------------->|
```

- **Inserts** (freezer, shifter, folder, crusher, filter, flanger, slicer): replace the
  signal while on and stop the moment they're off. The freezer comes first, so it captures
  the clean sound and everything after it works on the repeats. The folder folds the clean
  signal and the crusher grinds the folds; the filter sweeps both, and the flanger sweeps
  what they made. The slicer is the last insert, so it chops everything,
  including the resonator's ringing.
- **Resonator** (7th white key): a comb feedback loop. While on, it taps the signal after the
  flanger and feeds it back in after the freezer, so it rings through the shifter, folder,
  crusher, filter and flanger whenever they're on.
- **Sends** (delay, reverb): the key opens the effect's input, and its output is added to the
  signal, so tails ring out after the key is released. The delay gets the inserts' output;
  the reverb gets that plus the delay's echoes, so the echoes are reverberated while both
  are on.

| Control | Function |
|---|---|
| Hold an FX key | Effect on while held |
| SHIFT + FX key | Latch on / off; a latched effect stays on after release. Either may go down first: SHIFT then the key, or the key then SHIFT |
| FX key on a latched effect | Clears the latch; the effect stays on until the key is released |
| Knobs 1-4 | The parameters of the most recently pressed FX key, 1% per detent; stepped ones (shifter shift, filter LFO and delay divisions, freezer length and roll, slicer pattern and stereo) move one step per 3 detents |
| SHIFT + knobs 1-4 | Coarse: jumps to the next point of the parameter's grid per detent (see below) |
| SHIFT + press knobs 1-4 | Resets that parameter to its default |
| Press knobs 1-4 | Nothing yet (kept free for a second parameter page) |

The FX keys are dimly lit in their effect's colour while off and at full brightness while
on, where the audio coming out of the effect pushes the colour towards white, from -30 dBFS
up, the peaks most. The delay and reverb keys follow their returns, so after release they
glow with the tail, fading from full back to dim. The knob LEDs show the parameter values
in the effect's colours; a knob the effect doesn't use is dark and does nothing. Values
reset at power-off unless they're saved in a [scene](#fx-scenes).

| Key | Effect | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|---|
| 1st white | Freezer: Kastle 2 FX Wizard's, as a beat repeat | Length: 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1 bar (default 1/8) | Feedback: the input overdubbed into the repeats (default 0, pure repeat) | Stereo: the left loop up to 45 ms longer (default off) | Roll: the loop halves as it repeats, down to 1/64 bar: off, slow, … fast, one step per 3 detents (default off) |
| 2nd white | Shifter: a two-tap pitch shifter, with Kastle 2 FX Wizard's swoop and feedback | Shift in semitones, -12 to +12, one step per 3 detents; centre off (default +7, a fifth) | Swoop: on the key press, the shift pushes up to 2 octaves further and falls back over 1 s (default off) | Feedback: the shifted sound spirals (default off) | Stereo: the right channel up to a semitone higher (default off) |
| 3rd white | Folder: a sine-to-triangle wavefolder, antialiased and level-matched | Drive: 1x up to 32x, from barely folding to many folds (default 50%, 5.7x) | Shape: sine (smooth) to triangle (bright) (default sine) | Symmetry: a bias for uneven folds and even harmonics (default off) | Tone, lowpass 200 Hz to open (default open) |
| 4th white | Crusher: TEMPO's sample-rate reducer plus bit reduction | Rate, 21.6 kHz down to 480 Hz, halving every 18% of the knob (default 24%, 8.6 kHz) | Bits, 16 down to 2 (default 50%, 9 bits) | Tone, lowpass 200 Hz to open (default open) | XOR: flips bits of every sample for a digital buzz, from Kastle 2 FX Wizard's crusher (default off) |
| 5th white | Filter: the DJ filter from TAPE, TEMPO and WAVE (WAVE's copy) | Cutoff: lowpass left of centre, highpass right, flat at centre (default 30%, lowpass) | Resonance (default 50%) | LFO depth (default off) | LFO division: 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars (default 1 bar) |
| 6th white | Flanger: Kastle 2 FX Wizard's | Rate, 0.02 Hz to 50 Hz (default 0.55 Hz) | Amount: sweep depth and mix together, the top is pure vibrato (default 50%) | Feedback, up to 85% (default 50%) | Stereo: the right LFO runs free and detuned (default off) |
| 7th white | Resonator: the comb Kastle 2 FX Wizard runs around every mode | Pitch, 22 Hz to 880 Hz (default 110 Hz) | Feedback, up to 98% (default 70%) | Tone: the loop's lowpass, 1 kHz to 15 kHz (default 6.6 kHz) | Stereo: the right channel up to 12 semitones higher (default off) |
| 8th white | Slicer: Kastle 2 FX Wizard's rhythmic gate | Pattern, 8 steps of 16ths: `x.......`, `x...x...`, `..x...x.`, `x....x..`, `x..x..x.`, `x.x.x.x.`, `x.x.xx..`, `xxxxxxxx` (default `x..x..x.`) | Decay, 10 ms to 1 s (default 100 ms) | Chance: each step flipped at random, up to 90% (default off) | Stereo: the left channel plays a pattern up the list, the right one down, 0-7 apart (default off) |
| 2nd-to-last white | Delay: TEMPO's tempo-synced delay | Division: 1/8, 1/4T, 1/4, 1/2T, 1/4., 1/2, 1/2., 1 bar, 2 bars (default 1/4) | Feedback (default 40%) | Random: left of centre retrigger / reverse / pitch events, right octave-up shimmer with random pan, centre off (default off) | Level (default 70%) |
| Last white | Reverb (TEMPO's / WAVE's) | Decay (default 60%) | Tone, dark to open (default 60%) | Diffusion (default 60%) | Level (default 70%) |

SHIFT + turn moves one point of a fixed grid per detent, always to the next point in the
direction you turn, so a value set finely snaps onto the grid with the first coarse move:

| Parameter | Coarse grid |
|---|---|
| Resonator pitch | The notes at A440 (110 Hz is A2), F#0 to A5 |
| Shifter shift | -12, -7, -5, 0, +5, +7, +12 semitones (octaves, fifths, fourths) |
| Shifter stereo | Quarter semitones |
| Crusher rate | 48 kHz divided by 4, 8, 16, 32, 64: 12 kHz, 6 kHz, 3 kHz, 1.5 kHz, 750 Hz |
| Crusher bits | Whole bits |
| Folder drive | Doublings: 1x, 2x, 4x, 8x, 16x, 32x |
| Folder and crusher tone | Octaves down from open: 20 kHz, 10 kHz, 5 kHz … 312 Hz |
| Resonator tone | Octaves down from 15 kHz: 7.5 kHz, 3.75 kHz, 1.9 kHz |
| Stepped parameters (freezer length and roll, filter LFO and delay divisions, slicer pattern and stereo) | One step per detent instead of per 3 |
| Everything else, filter cutoff included | 10% steps |

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
- **Roll:** a beat repeat's build-up, which Kastle doesn't have. The four steps after off
  halve the loop after 8, 4, 2 or 1 repeats at the starting length, and every stage after
  that lasts as long as the first: at 1/8 and the slowest step, a bar of 1/8s, a bar of
  1/16s, a bar of 1/32s, then 1/64s until release. Each halving repeats the start of the
  capture, following on from the last repeat through the seam crossfade. Turning it off
  goes back to the full length.
- Lengths follow the delay's tempo. The loop seam has a 5 ms crossfade (shorter on loops
  under 20 ms), which Kastle doesn't have.
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
- Two taps read the input at the shifted speed, each for a 30 ms stretch before it starts
  over, crossfaded so the level stays even. Where a tap starts over is searched for, within
  7.5 ms, so it lines up with the other one: the pitch lands within about a cent of the
  interval and doesn't wobble.
- Kastle's shifter was a single tap that faded out and in at every wrap: its pitch knob
  slid from a slight detune to a buzzing, ring-mod-like tone, too coarse to set an
  interval. FRIZZ keeps its swoop, feedback and stereo, not the buzz.
- **Swoop** is Kastle's trigger envelope: 0.1 s up, 1 s back. Kastle tied its depth to TIME;
  here it has its own knob.
- **Feedback** sends the shifted output back into the delay, so each pass shifts again: a
  fifth stacks into fifths (Kastle: its comb around every mode).
- LEDs: the key is red; the shift knob goes blue (down) through white (off) to red (up).

Folder details:
- **The fold:** past the fold point the signal is mirrored back, again and again as the
  drive rises, each fold adding harmonics. Drive is the folding: on loud material the folds
  start low on the knob, on quiet material higher up.
- **Level:** a folder's output is about full scale whatever goes in, so the output is
  matched to the input's level (over ~50 ms, by up to 2x louder): punching in changes the
  sound, not the loudness.
- **Symmetry** shifts the fold by up to a quarter of its period: at the top, the sine fold
  becomes a cosine, all even harmonics, an octave-ish edge. The DC the bias adds is blocked.
- **Aliasing:** the sine fold hardly aliases: on a sine below about 1.5 kHz the aliases stay
  more than 85 dB down at any drive. The triangle's corners do alias: first-order ADAA
  takes off 5 to 13 dB, but on bright material high up the drive it still adds some grit,
  as DaisySP's `Wavefolder` (the triangle alone, unfiltered) does more of. (DaisySP's `Fold`
  isn't a folder: it's a sample-rate reducer, the crusher's rate knob.)
- In the resonator's loop, the folder folds the ringing on every trip.
- LEDs: the key is magenta; the knobs go magenta through white to orange.

Crusher details:
- **Dive:** every press of the key drops the rate up to 10x over 0.1 s and lets it recover
  over 0.4 s, Kastle's trigger dive.
- **XOR** flips fixed bits of each sample as 16-bit (Kastle's constants, up to 4000), a buzz
  that's loudest where the signal crosses zero. On its own XOR would turn silence into a
  constant offset, so what it adds is DC-blocked.

Resonator details:
- The loop holds a soft clipper, a lowpass (Tone) and a 50 Hz highpass, as on Kastle, so it
  saturates rather than runs away, whatever is inside it. On its own it's a comb on the dry
  sound; with the inserts inside the loop on, the ringing goes through them on every trip:
  through the shifter it spirals, through the crusher it turns grainy, and the filter shapes
  it the way a filter in a dub delay's feedback does. With the freezer on, it rings the
  repeats; with the slicer on, the ringing is chopped into bursts.
- Like Kastle, it turns the input down as feedback goes up (by half at the most). Kastle's
  comb is fixed per mode, 22-440 Hz with about 40% feedback at most; this one is tunable
  and stronger.
- LEDs: the key is lime; the knobs go orange through white to light blue.

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

## FX scenes

A scene holds every effect's four knob values and whether it's latched. There are four slots,
on the first four dark keys from the left (C#, D#, F#, G# of the lower octave). Scenes are
saved to the SD card and come back at power-on. A scene is what you last saved into it:
changes you make after recalling one are only kept if you save again.

| Control | Function |
|---|---|
| Scene key | Recall the scene at once: every effect's knobs jump to it, the effects it latched come on and all others are unlatched. FX keys you're holding stay on |
| Scene key of the active scene | Back to the scene as saved, dropping your changes |
| Scene key of an empty slot | Nothing (the key blinks red) |

A recall is meant for performing, a build-up on one scene and the drop on the next:

- **The knobs land within about 25 ms** instead of gliding over 100 ms like a turned knob.
  Stepped values (freezer length, slicer pattern, delay division …) switch at once.
- **An effect a recall turns on starts as if you'd pressed its key:** the freezer grabs fresh
  audio, the shifter swoops, the crusher dives, the flanger's sweep restarts, the slicer
  attacks.
- **An effect latched in both scenes keeps running** without restarting. A freeze held
  through the drop stays frozen.
- **Delay and reverb tails ring out** after a scene unlatches them.
- The input/loop mix, the volumes, the compressor and the looper aren't part of a scene.

Save, copy and delete work like TAPE's and TEMPO's preset keys, on the last three dark keys,
no SHIFT needed:

| Key | Colour | Function |
|---|---|---|
| Last dark key (A# of the upper octave) | blue | **Save** the current effect settings into a slot |
| 2nd-to-last dark key (upper G#) | green | **Copy** one slot into another, without changing the sound |
| 3rd-to-last dark key (upper F#) | red | **Delete** a slot |

1. **Tap the function's key.** It lights up fully; the scene keys now select slots instead of
   recalling them. The effects and knobs keep working.
2. **Tap the slot.** It blinks in the function's colour. For copy, tap the source first
   (it stays lit), then the destination. Copy and delete need a saved slot.
3. **Press the CHOMPI key**, which blinks red once there's something to confirm. The slot
   flashes green and the function ends.

**To get out without doing anything, tap the function's key again.** Tapping a different
function key switches to that function.

The slot flashes red instead of green when there's no SD card, or the card couldn't be
written: the change works until power-off but isn't stored. The next save, copy or delete
tries the card again.

The scene keys show the slots: dark when empty, dimly white when saved, bright for the scene
you recalled or saved last, and pulsing once you've turned a knob or changed a latch since.
The save, copy and delete keys are dimly lit in their colour.

The scenes live in `FRIZZ/frizz_scenes.txt` on the card, one line per effect, keyed by its
name. FRIZZ creates the folder on its first start (moving the file from the card root, where
older versions kept it); after that the card is written only when you save, copy or delete,
never on a recall. Saving takes a moment in which the LEDs may pause.

## MIDI clock

Quantized recording follows MIDI clock (24 PPQN) from the TRS MIDI input or USB. CHOMPI is a
USB device, so USB clock comes from a computer or a host. Whichever source ticks first is
used, until it has been silent for 0.5 s. Only clock is read; there's no MIDI out.

## Hardware self-test

Holding the VOLUME knob down at power-on enters the factory QC test that FRIZZ inherited from
the stock firmware, instead of FRIZZ. You won't need it for normal use. To get back to FRIZZ,
power off and on again.
