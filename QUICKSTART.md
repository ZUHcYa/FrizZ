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
dry path around the effects.

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
- **SHIFT + key:** latch the effect on. Do it again to turn it off.
- **Knobs 1-4:** shape the effect whose key you pressed last. Their LEDs show the settings;
  a dark knob does nothing for that effect.
- **Press a knob:** reset that setting.

Delay and reverb tails keep ringing after you let go. The other effects stop the moment you
release the key. Settings reset when you switch off.

Things to try first:

- **Freezer (1st key)** on a beat: it grabs 1/8 from the moment you press and repeats it while
  you hold the key. Turn knob 4 for a roll that speeds up.
- **Shifter (2nd key):** a fifth up by default. Knob 1 sets the interval.
- **Resonator (7th key)** with the crusher or the shifter latched: the ringing goes through them on
  every pass.
- **Delay + reverb (the last two keys):** tap them to throw echoes and tails onto single notes.

With MIDI clock, the freezer, slicer, filter LFO and delay follow the tempo. Without it they run
at the last tempo received, or 120 BPM.

## Where next

- [Manual](MANUAL.md): what every knob does on every effect
- [Install guide](INSTALL.md): updating FRIZZ, going back to stock firmware
