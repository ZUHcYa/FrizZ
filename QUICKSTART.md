# FRIZZ quick guide

Your first ten minutes with FRIZZ. Every detail is in the [manual](MANUAL.md).

## How the sound flows

```
AUX in -> input gain -+-----------------+
                      |                 +-> input/loop mix -> effects (white keys) -> output volume -> compressor -> out
                      +-> looper -------+
```

The looper records the input **before** the effects, so you can play effects over a loop and
nothing gets printed into it. The effects come **after** the mix, so they act on everything you
hear, the live input as well as the loop. The mix knob balances input against loop; there's no
dry path around the effects on the master out. The headphones follow the mode switch: down,
the same as the master out; up, only the dry input (no loop, no effects).

**SHIFT** means holding the CHOMPI key (with the mode switch in either position). The key lights
white while it acts as SHIFT.

## 1. Set your levels

| Do this | To set |
|---|---|
| Turn VOLUME | Output volume. The LED is a level meter |
| Press VOLUME, then turn | Input gain. The LED runs from blue to red |
| Press again, then turn | Master compressor, off by default |
| Press again | Back to output volume |
| SHIFT + turn VOLUME | Input/loop mix: green = only the input, purple = only the loop |

## 2. Record a loop

1. Press **LOOP** to start recording. LOOP turns red.
2. Play something.
3. Press **LOOP** again. The loop plays back immediately, and the mix jumps to only the loop, so you
   hear only the loop. Turn SHIFT + VOLUME back towards green to play along with it.
4. **PLAY** pauses and resumes it.
5. Turn the big **transport knob** to change speed in fifths and octaves, down to 1/16× and
   on into reverse. While paused, turning the knob scrubs. Press the knob to get back to normal speed.
6. Hold **PLAY + LOOP** for 2 seconds to erase the loop. The mix jumps back to the input.

**In sync with a clock:** with MIDI clock coming in over TRS or USB, hold PLAY and press LOOP.
The recording then ends on a whole bar. Without a clock, LOOP blinks red three times.

A loop can be up to 2:45 long and is gone when you switch off.

## 3. Play the effects

Ten white keys hold effects, counted from the left, in the order the sound goes through them.
The white keys between the slicer and the delay do nothing.

| White key | 1st | 2nd | 3rd | 4th | 5th | 6th | 7th | 8th | 2nd-to-last | last |
|---|---|---|---|---|---|---|---|---|---|---|
| Effect | Freezer | Shifter | Folder | Crusher | Filter | Flanger | Resonator | Slicer | Delay | Reverb |

- **Hold a key:** the effect is on while you hold it.
- **SHIFT + key** (either first): latch the effect on. Do it again to turn it off.
- **Knobs 1-4:** shape the effect whose key you pressed last. Their LEDs show the settings;
  a dark knob does nothing for that effect. The knobs follow one pattern: 1 is the main
  control, 2 the feedback, 3 the tone or colour, 4 the stereo width or level (where an effect
  has no such setting, it puts another one there).
- **SHIFT + turn a knob:** jump in big steps: notes, octaves or musical intervals where the
  setting has them, 10% otherwise.
- **SHIFT + press a knob:** reset that setting.

Every effect starts silent or nearly so: turn its knob 1 (on the delay and reverb, knob 4,
the level) to bring it in.

Delay and reverb tails keep ringing after you let go. The other effects stop the moment you
release the key. Settings reset when you switch off, unless you save them as a scene.

Things to try first:

- **Freezer (1st key)** on a beat: SHIFT + turn knob 1 down five clicks to 1/8. It grabs that
  much from the moment you press and repeats it while you hold the key. Turn knob 3 for a
  roll that speeds up.
- **Shifter (2nd key):** SHIFT + turn knob 1 up two clicks for a fifth.
- **Resonator (7th key)** with the crusher or the shifter latched: the ringing goes through them on
  every pass.
- **Delay + reverb (the last two keys):** turn up their knob 4, then tap them to throw echoes and
  tails onto single notes.

With MIDI clock, the freezer, slicer, filter LFO and delay follow the tempo. Without it they run
at the last tempo received, or 120 BPM.

## 4. Save scenes

The dark keys of the lower octave hold scenes: every effect's settings and latches. The first
one is the blank scene: it turns every effect off and resets the knobs, and can't be saved
over. The other four are yours.

- **Save:** tap the last dark key (blue), tap a scene key, press CHOMPI to confirm.
- **Recall:** press a scene key. Try a build-up on one scene and the drop on the next, then
  the first key to turn every effect off.
- **Copy and delete:** the same steps with the green and red dark keys. Tap the
  function's key again to back out.

While you save, copy or delete, the scene keys you can tap light in the function's colour,
and the CHOMPI key blinks in it when there's something to confirm. White means done.

Scenes are stored on the SD card. See the [manual](MANUAL.md#fx-scenes) for the details.

## Where next

- [Manual](MANUAL.md): what every knob does on every effect
- [Install guide](INSTALL.md): updating FRIZZ, going back to stock firmware
