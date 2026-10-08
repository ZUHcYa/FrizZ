# FRIZZ quick guide

Your first ten minutes with FRIZZ. Every detail is in the [manual](MANUAL.md).

## How the sound flows

```
AUX in -> input gain -+-----------------+
                      |                 +-> input/loop mix -> effects (white keys) -> compressor -> output volume -> out
                      +-> looper -------+
```

The looper records the input **before** the effects, so you can play effects over a loop and
nothing gets printed into it. The effects come **after** the mix, so they act on everything you
hear, the live input as well as the loop. The mix knob balances input against loop; there's no
dry path around the effects on the master out. The headphones carry the same as the master
out, or, from VOLUME's page 4, only the dry input (no loop, no effects).

**SHIFT** means holding the CHOMPI key. The key lights white while it acts as SHIFT.

## 1. Set your levels

| Do this | To set |
|---|---|
| Turn VOLUME | Output volume. The LED is a level meter |
| Press VOLUME, then turn | Input gain. The LED runs from blue to red |
| Press again, then turn | Mono: turn left for a mono cable (white), right for stereo (light blue) |
| Press again, then turn | Headphones: white = the master out, green = only the dry input |
| Press again | Back to output volume |
| SHIFT + turn VOLUME | Input/loop mix: green = only the input, purple = only the loop |
| SHIFT + press VOLUME | Mix back to the loop only (with a loop) or the input only (without) |

Each press blinks the page's number in white: once for output, twice for input, three times
for mono, four times for the headphones.

## 2. Record a loop

1. Press **LOOP** to start recording. LOOP turns red.
2. Play something.
3. Press **LOOP** again. The loop plays back immediately, and the mix jumps to only the loop, so you
   hear only the loop. Turn SHIFT + VOLUME back towards green to play along with it.
4. **PLAY** pauses and resumes it.
5. Turn the big **transport knob** to change speed in fifths and octaves, down to 1/16× and
   on into reverse. While paused, turning the knob scrubs. Press the knob to get back to normal speed.
6. Press **LOOP** to erase the loop. The mix jumps back to the input. To erase at the end of
   the loop instead, hold PLAY and press LOOP.

**In sync with a clock:** with MIDI clock coming in over TRS or USB, hold PLAY and press LOOP.
The recording then ends on a whole bar. Without a clock, LOOP blinks red three times.

A loop can be up to 2:45 long and is gone when you switch off.

## 3. Play the effects

The first twelve white keys hold effects, counted from the left, in the order the sound goes
through them. The 13th does nothing, the 14th is the master compressor and the last one the
randomizer (both below).

| White key | 1st | 2nd | 3rd | 4th | 5th | 6th | 7th | 8th | 9th | 10th | 11th | 12th |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Effect | Freezer | Shifter | Folder | Crusher | Filter | Flanger | Resonator | Slicer | Wow & flutter | Tape stop | Delay | Reverb |

- **Hold a key:** the effect is on while you hold it.
- **Hold a key, then SHIFT:** latch the effect on. Do it again, or just tap the key, to turn
  it off.
- **SHIFT, then a key:** pick the effect for the knobs without hearing it, to set it up
  before you punch it in. The key flashes white.
- **Knobs 1-4:** shape the effect whose key you pressed or picked last. Their LEDs show the
  settings; a dark knob does nothing for that effect. The knobs follow one pattern: 1 is the
  main control, 2 the feedback, 3 the tone or colour, 4 the stereo width or level (where an
  effect has no such setting, it puts another one there).
- **SHIFT + turn a knob:** jump in big steps: notes, octaves or musical intervals where the
  setting has them, 10% otherwise.
- **SHIFT + press a knob:** reset that setting.

Every effect starts silent or nearly so: one knob brings it in. That's knob 1 on most, knob 2
(feedback) on the resonator, knob 3 (amount) on the flanger and knob 4 (level) on the delay
and reverb. The freezer, slicer and tape stop work from the first press: the freezer repeats
a whole bar, the slicer pumps gently and the tape stop stops.

**Master compressor:** press the second-to-last white key, and knobs 1-4 set the compressor on the
master out: amount (off at first), ratio, speed and mix. Its key lights up as it compresses.
It's always on, and FRIZZ remembers its settings when you switch off. Press an effect key to
give the knobs back to the effect.

**Randomizer:** hold the last white key, and it plays the effects for you: on every gate of
a one-bar pattern it switches 1 to 5 of them on, with random settings. Knob 1 picks the
pattern, 2 the gate length, 3 the chance and 4 shifts every gate a little late. Latch it like
an effect. See [Randomizer](MANUAL.md#randomizer).

Delay and reverb tails keep ringing after you let go. The other effects stop the moment you
release the key, except the tape stop, which spins back up first. Settings reset when you switch off, unless you save them as a scene.

Things to try first:

- **Freezer (1st key)** on a beat: hold SHIFT, tap the key, and keep holding SHIFT while you
  turn knob 1 down five clicks to 1/8 (without SHIFT, a step takes three clicks). It grabs that much from the moment you press and repeats it while you hold the
  key. Turn knob 3 for a roll that speeds up.
- **Shifter (2nd key):** hold SHIFT, tap the key, and with SHIFT still held turn knob 1 up two
  clicks for a fifth, then hold the key to hear it.
- **Resonator (7th key)** with the crusher or the shifter latched: the ringing goes through them on
  every pass.
- **Delay + reverb (the 11th and 12th keys):** turn up their knob 4, then tap them to throw echoes and
  tails onto single notes.
- **Tape stop (10th key)** with the delay or reverb latched: hold it on a beat and the music
  winds down over half a bar while the tails ring on. Let go and it spins back up.

The freezer, slicer, filter LFO, tape stop, delay and randomizer follow a tempo: the loop's while there is one, so
they lock to it; otherwise MIDI clock, or the last tempo (120 BPM at power-on). Hold SHIFT
and tap LOOP three times or more to tap a tempo, or to tell FRIZZ how many beats a loop has.
See [Tempo](MANUAL.md#tempo).

## 4. Save scenes

The dark keys of the lower octave hold scenes: every effect's settings and latches. The first
one is the blank scene: it turns every effect off and resets the knobs, and can't be saved
over. The other four are yours.

- **Save:** tap the last dark key (blue), tap a scene key, tap CHOMPI to confirm.
- **Recall:** press a scene key. Try a build-up on one scene and the drop on the next, then
  the first key to turn every effect off.
- **Morph:** hold CHOMPI and press a scene key, then let go of CHOMPI: the effects glide there
  and land at the end of the bar. Before you let go, tap the key again for one bar more per
  tap; the glide starts when you let go. Hold CHOMPI and press PLAY to stop it where it is.
- **Copy and delete:** the same steps with the green and orange dark keys. Tap the
  function's key again to back out.

While you save, copy or delete, the scene keys you can tap light in the function's colour,
and the CHOMPI key blinks in it when there's something to confirm. White means done.

Scenes are stored on the SD card. See the [manual](MANUAL.md#fx-scenes) for the details.

## Where next

- [Manual](MANUAL.md): what every knob does on every effect
- [Install guide](INSTALL.md): updating FRIZZ, going back to stock firmware
