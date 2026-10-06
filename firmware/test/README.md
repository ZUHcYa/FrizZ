# FRIZZ engine harness

The only automated check FRIZZ has off the device. It compiles FRIZZ's audio engine
(`code/src/passthroughEngine.h` and everything it includes) with the host's `g++`, runs a fixed
script of key presses and knob turns through it, and writes every output sample and FX meter
to a file. Two versions of the engine can then be compared.

```bash
./check.sh                  # HEAD against the working tree
./check.sh 9da090e 647185b  # any two git refs ("work" = the working tree)
STRESS=1 ./run.sh work out.bin
```

- **A change that shouldn't alter the sound** (a refactor, a rename): `check.sh` should print
  `bit-identical` and exit 0. Anything else means the sound changed.
- **A change that should** (a new effect, a new order, a retuned knob): `check.sh` prints the
  master out's loudness per segment for both versions, the peak and any NaNs, so a blow-up or
  an effect gone silent stands out.

What it covers: the whole engine as the audio callback runs it, with the delay's random events
seeded and MIDI clock absent (the tempo clock runs on its internal 120 BPM). What it doesn't:
the play page and the looper's recording (see `controls.sh` and `looper.sh` below), MIDI
clock, and anything about the hardware.

The script (`harness.cpp`) is a segment of 3 s per FX plus four: each FX on its own with a random knob turned
every 0.25 s, the inserts together, everything together, then the tails. `NOFX=1` runs it with
no FX switched on, to check that a segment actually exercises its effect; `STRESS=1` runs
everything at once with the resonator's loop at its most extreme for the whole run.

It drives the engine through `Init`, `SetFxOn`, `SetFxParam` and `GetFxLevel` with the
`chompi::FX_*` names, so it builds against FRIZZ from `9da090e` on. If that interface changes,
update `harness.cpp` along with it.

## Shifter pitch check

```bash
./pitch.sh
```

Runs a 220 Hz sine and a 220 Hz harmonic tone through the shifter (`FxShifter.h`, working
tree) at every interval from -12 to +12 semitones, and fails if any comes out more than 5
cents off or its level wobbles by more than 2 dB. The engine harness can't tell a shifter
that's out of tune from one that isn't; this can. Takes about 12 s. (A plain two-tap shifter
is up to 80 cents off and wobbles up to 10 dB; Kastle's single-tap one fails 48 of the 50.)

## Scene file check

```bash
./scenes.sh
```

Checks the FX scene file (`FxScenes.h`): scenes written and read back come out within 1e-6
(a coarse grid point stays on its grid) and exactly on a second round trip. It also checks
that a hand-written file is read by effect name, with unknown effects and slots skipped and
missing ones on their defaults, and that a foreign, empty or truncated file doesn't break
anything. Last, it runs a filter jump through the engine with a recall's fast slew and with
a knob's. The recall must land sooner and within 30 ms, without a larger step between samples,
and hand back to the knobs' slew afterwards. It doesn't touch the card (`SceneStore.h`) or the
play page. Needs `run.sh` to have built DaisySP once.

## Play page, looper and tempo checks

```bash
./controls.sh
./looper.sh
./tempo.sh
```

`controls.sh` runs the play page's logic (`FxControls.h`, `SceneControls.h`) against a fake
engine that records what it's sent: holding and latching in either order, fine, stepped and
coarse knob turns, SHIFT + press, and the scene save / copy / delete / recall flow, including
that a recall sends only what changes, and what a scene morph does with each effect, its
taps and stopping it. `looper.sh` runs `Looper.h` without MIDI clock: a free
recording plays back frame for frame at its length, play / pause, erase, the speed ladder, and
the refused quantized record. `tempo.sh` checks the FX's tempo: tap tempo (`TapTempo.h`), how
a loop's beats are fitted or guessed, the tempo clock locked to a loop (`TempoClock.h`: beat 1
on the loop's start, counting down in reverse, standing still when paused, the tempo times the
speed, a tap refitting the beats, the loop overriding MIDI clock) and the beats of a loop
recorded quantized to a faked clock; then the bar lines (free, on 2-, 4- and 6-beat loops, in
reverse) and a scene morph (`FxMorph.h`) on them: landing on the bar line, one per tap, the
glide, fades in and out, stopping it halfway. None covers the LEDs or `NormalPage.h`'s routing of the
keys. All need `run.sh` to have built DaisySP once.

`host/` holds the stand-ins for the parts of libDaisy the engine touches: `daisy.h` (two sample
conversions) and `MidiClock.h` (no clock, unless a test sets its fields, as `tempo.cpp`
does). DaisySP is compiled for the host once into `build/`,
which is ignored.
