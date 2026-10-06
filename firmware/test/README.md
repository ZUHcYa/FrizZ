# FRIZZ engine harness

The only automated check FRIZZ has off the device. It compiles FRIZZ's audio engine
(`code/src/passthroughEngine.h` and everything it includes) with the host's `g++`, runs a fixed
script of key presses and knob turns through it, and writes every output sample and FX meter
to a file. Two versions of the engine can then be compared.

```bash
./all.sh                    # every check below, one line each; exits 0 when all pass
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
`chompi::FX_*` names, so it builds against FRIZZ from `9da090e` on, and sets the master
compressor's amount to 0.3 (`SetCompParam`, or `SetFinalComp` before `MasterComp.h`; the two
compressors differ, so a run across that change isn't bit-identical). If that interface changes,
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

## Tape FX check

```bash
./tape.sh
```

Runs a sine through wow & flutter (`FxWarble.h`) and the tape stop (`FxTapeStop.h`). Wow &
flutter on its defaults and the tape stop while off must pass the input bit for bit. The
flutter's pitch wobble, measured cycle by cycle, must stay within its bound (the depth
times the wobbles' rates) and reach at least 30% of it. TAPE's wow at the top must stay
finite and move the pitch. A stop must be silent once its time is up, on a linear curve and
a brake, also 2 bars at 50 BPM. Every spin-up, or none, must end on the input bit for bit,
also after releasing mid-stop and pressing mid-spin-up, with no step between samples larger
than a crossfade's.

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
play page.

## Master compressor check

```bash
./comp.sh
```

Runs `MasterComp.h` on its own: amount 0 and mix 0 are exact bypasses, the static curve at
each end of the ratio and the amount (within 0.2 dB, the soft knee too), the attack and
release slowing from fast to slow speed, and the linked stereo. Then the whole engine with
everything up and a full-scale square starting from silence, so the outputs must stay within
1.0 through the safety limiter. Last, the settings file (`MasterSettings.h`): it round-trips,
grid points come back exactly, and foreign, unknown or partial files are read sensibly, the
randomizer's line too, and a file from before it keeps its knobs on their defaults. It
doesn't touch the card or the play page.

## Randomizer check

```bash
./randomizer.sh
```

Runs `FxRandomizer.h` on a 120 BPM clock: every pattern fires exactly its gates at chance 1,
none at chance 0 and about half at .5; the pulse width sets the gate's length (20 ms at
least), the shift delays every gate alike. Over 40 bars of 16ths, each gate picks 1-5
effects, never a send or the freezer, never one whose key is on and never one the gate
before had, still fading out. Then `FxChain.h` around it: a gate's effects are the randomizer's, a key coming
on takes its effect back at the next block, and once a closed gate's effects have faded out
they're the user's again. Turned on again mid-bar, it fires at once on a gate's 16th, and a
gate that was waiting out the shift when it went off doesn't fire later. Its level guard
(`LevelGuard`): untouched bit for bit while the randomizer has nothing, an output 12 dB over its
input held to +3 dB, a quieter one never turned up, back to exactly unity after the gate; and
through the chain, a -12 dBFS sine under the densest pattern for 32 bars never comes out more
than 3 dB (+1 dB for the attack) over its input in any 16th (it prints the loudest; about
+19 dB without the guard). The crusher's own guard: with XOR at full, a sine at
-40 dBFS comes out no louder than it went in (+22 dB without the guard), and one at -6 dBFS
no louder but still there. It doesn't check that the effects' knobs land at once
(`SnapParams`), nor what a gate sounds like.

## Play page, looper and tempo checks

```bash
./controls.sh
./keys.sh
./looper.sh
./tempo.sh
```

`controls.sh` runs the play page's logic (`FxControls.h`, `SceneControls.h`) against a fake
engine that records what it's sent: holding and latching in either order, fine, stepped and
coarse knob turns, SHIFT + press, and the scene save / copy / delete / recall flow, including
that a recall sends only what changes, and what a scene morph does with each effect, its
taps and stopping it; the compressor's key and knobs, which a recall leaves alone; and the
randomizer's key, held, latched and selected as an FX key is, which scenes leave alone. `keys.sh` runs the CHOMPI, PLAY and LOOP keys (`PlayKeys.h`) against a
fake host: the confirm tap, SHIFT combos, the looper's combos and the erase hold, and that a
release without its press does nothing. `looper.sh` runs `Looper.h` without MIDI clock: a free
recording plays back frame for frame at its length, play / pause, erase, the speed ladder, and
the refused quantized record. `tempo.sh` checks the FX's tempo: tap tempo (`TapTempo.h`), how
a loop's beats are fitted or guessed, the tempo clock locked to a loop (`TempoClock.h`: beat 1
on the loop's start, counting down in reverse, standing still when paused, the tempo times the
speed, a tap refitting the beats, the loop overriding MIDI clock) and the beats of a loop
recorded quantized to a faked clock; then the bar lines (free, on 2-, 4- and 6-beat loops, in
reverse) and a scene morph (`FxMorph.h`) on them: landing on the bar line, one per tap, the
glide, fades in and out, stopping it halfway. None covers the LEDs or `NormalPage.h`'s routing of the
keys to these classes.

`host/` holds the stand-ins for the parts of libDaisy the engine touches: `daisy.h` (two sample
conversions) and `MidiClock.h` (no clock, unless a test sets its fields, as `tempo.cpp`
does). DaisySP is compiled for the host once into `build/`,
which is ignored.

## How the scripts are built

Every script sources `lib.sh`, which builds DaisySP for the host once (into `build/`) and has
`unit_test NAME`: it copies the working tree's headers next to the host `MidiClock.h`, builds
`NAME.cpp` with warnings on and runs it. A test's own warnings are shown; if it doesn't build,
so is every message, including the headers'. The `.cpp` checks share `check.h` (`Check()`,
`Finish()`). A new check is a `NAME.cpp` plus a two-line `NAME.sh`, and a line in `all.sh`.
