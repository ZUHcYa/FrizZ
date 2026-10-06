# Effects across the firmwares

What each firmware's audio path does, as found in the source. TAPE, TEMPO and WAVE are
independent forks, so a same-named file (`reverb.h`, `DJFilter.h`, …) is a separate copy in each
`code/src/` (under `reference/firmware/`) and may differ. FRIZZ is the custom firmware in
`firmware/`, forked from WAVE.

None of the three original firmwares does real bit-depth reduction. The closest is TEMPO's
sample-rate reducer (decimation). TAPE's "lofi" knob page is saturation.

## Overview

| Effect | TAPE | TEMPO | WAVE | Implementation |
|---|:-:|:-:|:-:|---|
| **DJ filter** (one knob: LP ← off → HP, with resonance) | ✅ | ✅ | ✅ | `DJFilter.h` on top of `BasicMMF.h` |
| **Saturation / soft clip** (drive into `daisysp::SoftClip`, plus makeup gain) | ✅ | ✅ | ✅ | inline in the engine |
| **Reverb** (Rings/Clouds Griesinger topology: amount, time, lowpass, diffusion) | ✅ | ✅ (+ freeze) | ✅ | `reverb.h` + `fx_engine.h` |
| **Delay** (mono-in ping-pong, int16 in SDRAM) | ✅ | – | ✅ | `InterpolatedDelayLine.h` |
| **Tempo-synced delay** (clock divisions, random retrigger / reverse / pitch events, octave-up shimmer with random pan, buffer freeze) | – | ✅ | – | `granularDelay.h` (`granularDelay2.h` exists but isn't used) |
| **Wow & flutter ("warble")** (randomly modulated short delay) | ✅ | – | – | `Warble.h` |
| **Sample-rate reduction** (decimation) | – | ✅ | – | `daisysp::SampleRateReducer` in `SampleEngine.h` / `SliceEngine.h` |
| **Ducking compressor** (wet ducked by dry) | – | ✅ | – | `SimpleCompressor.h` |
| **Output compressor / limiter** ("final comp") | ✅ | ✅ | ✅ | `limiter.h` |
| **Filter LFO** (triangle, modulates cutoff) | – | – | ✅ | `subtractiveEngine.h` |
| **Pitch LFO / vibrato** (±2 semitones) | – | – | ✅ | `subtractiveEngine.h` |
| **Pan** | ✅ (per slot) | ✅ | ✅ | inline |
| **Reverse / varispeed** (negative pitch = reverse) | ✅ (voices + looper) | ✅ | – | sampler / player |
| **Looper** (record, scrub, varispeed) | ✅ | – | – | `LooperEngine.h` |

Utility processing, not creative effects: the mic input conditioning (`MicFilter.h`: a 150 Hz
high-pass and 5 notch filters against feedback), DC blockers, and the envelope followers that
drive the VU LEDs.

## Signal chain per firmware

- **TAPE** (`DSPEngine.h`): voices → soft clip → `ApplyFx` = DJ filter → saturation → warble →
  delay → reverb, a block that can sit before or after the looper (`fx_pre_loop`) → output
  compressor. The magic knob has three pages: reverb/delay, **lofi** (= saturation) and filter.
  Warble, filter resonance and delay time are in the SHIFT menu.
- **TEMPO** (`FxEngine.h`): each engine (chromatic, slice) runs pan → sample-rate reducer → DJ
  filter, then splits into a dry and a wet bus. Wet → granular delay → reverb → ducking compressor
  (keyed from dry), then output compressor → saturation. `setMasterRes` exists but no UI calls
  it, so TEMPO's filter has no resonance control.
- **WAVE** (`subtractiveEngine.h`): per voice wavetable → amp envelope → its own DJ filter, with a
  shared filter LFO and pitch LFO; voices summed → delay → reverb → output compressor →
  saturation → pan.
- **FRIZZ** (`FxChain.h`): AUX input → dry/wet mix with the looper → freezer → shifter →
  folder → crusher → filter → flanger → slicer → wow & flutter → tape stop → delay → reverb
  (fed the delay's echoes too) → output compressor. The resonator's comb loops from after the
  flanger back to after the freezer.
  Each effect is a punch-in key, and the keys run in this order left to right.

## Unused effects in the vendored DaisySP

DaisySP is vendored in every firmware (`code/libs/DaisySP/Source/`) and its prebuilt
`libdaisysp.a` is linked, but the firmwares use little of it. Available without writing new DSP:

- **Effects:** `bitcrush`, `decimator` (bit depth + downsampling in one), `sampleratereducer`,
  `overdrive`, `fold` (a sample-and-hold rate reducer despite the name), `wavefolder` (an
  unfiltered triangle fold; FRIZZ's folder is its own, antialiased), `chorus`, `flanger`,
  `phaser`, `tremolo`, `autowah`, `pitchshifter`, `reverbsc`
- **Filters:** `moogladder`, `svf`, `comb`, `biquad`, `allpass`, `tone` / `atone`, `mode`, `nlfilt`

Check the memory table after adding any of them, especially on TAPE, which has almost no SRAM
left.

## Ported to FRIZZ so far

FRIZZ's punch-in FX, one file each (`firmware/code/src/Fx*.h`), in signal order, which is also
their keys' order left to right (see the FRIZZ chain above). Their controls are in
[`MANUAL.md`](../MANUAL.md).

| Key | FRIZZ effect | Taken from |
|---|---|---|
| 1st white | Freezer (insert) | Bastl Instruments' Kastle 2 FX Wizard freezer (MIT) |
| 2nd white | Shifter (insert) | FRIZZ's own two-tap shifter with aligned splices, tuned in semitones; the swoop, feedback and stereo after Bastl Instruments' Kastle 2 FX Wizard shifter (MIT) |
| 3rd white | Folder (insert) | FRIZZ's own: a sine-to-triangle fold with first-order ADAA, after DaisySP's `wavefolder` (which is the triangle alone, unfiltered) |
| 4th white | Crusher (insert) | TEMPO's sample-rate reducer, plus FRIZZ's own bit quantizer and tone lowpass, and the XOR and trigger dive from Bastl Instruments' Kastle 2 FX Wizard crusher (MIT) |
| 5th white | DJ filter (insert) | WAVE's `DJFilter.h` + `BasicMMF.h`; the triangle LFO follows WAVE's filter LFO but is synced by `TempoClock.h` |
| 6th white | Flanger (insert) | Bastl Instruments' Kastle 2 FX Wizard flanger (MIT) |
| 7th white | Resonator (comb loop from after the flanger back to after the freezer) | the feedback comb Bastl Instruments' Kastle 2 FX Wizard runs around every mode (MIT) |
| 8th white | Slicer (insert) | Bastl Instruments' Kastle 2 FX Wizard slicer (MIT) |
| 10th white | Wow & flutter (insert) | TAPE's `Warble.h` as knob 1, plus FRIZZ's own flutter, tone and stereo |
| 11th white | Tape stop (insert) | FRIZZ's own: a varispeed read head on an SDRAM buffer, tempo-synced stop and spin-up |
| 2nd-to-last white | Delay (send) | TEMPO's `granularDelay.h`, clocked by FRIZZ's `TempoClock.h` from MIDI clock |
| Last white | Reverb (send, also fed the delay's echoes) | TEMPO's `reverb.h` + `fx_engine.h` |
