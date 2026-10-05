# Effects across the firmwares

What each firmware's audio path does, as found in the source. TAPE, TEMPO and WAVE are
independent forks, so a same-named file (`reverb.h`, `DJFilter.h`, …) is a separate copy in each
`code/src/` and may differ. FRIZZ is the custom firmware in `frizz/`, forked from WAVE.

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

## Unused effects in the vendored DaisySP

DaisySP is vendored in every firmware (`code/libs/DaisySP/Source/`) and its prebuilt
`libdaisysp.a` is linked, but the firmwares use little of it. Available without writing new DSP:

- **Effects:** `bitcrush`, `decimator` (bit depth + downsampling in one), `sampleratereducer`,
  `overdrive`, `fold` / `wavefolder`, `chorus`, `flanger`, `phaser`, `tremolo`, `autowah`,
  `pitchshifter`, `reverbsc`
- **Filters:** `moogladder`, `svf`, `comb`, `biquad`, `allpass`, `tone` / `atone`, `mode`, `nlfilt`

Check the memory table after adding any of them, especially on TAPE, which has almost no SRAM
left.

## Ported to FRIZZ so far

FRIZZ's punch-in FX (`frizz/code/src/PunchFx.h`; controls in `frizz/README.md`):

| Key | FRIZZ effect | Taken from |
|---|---|---|
| 1st white | DJ filter (insert): cutoff, resonance, LFO depth, LFO division | WAVE's `DJFilter.h` + `BasicMMF.h`; the triangle LFO follows WAVE's filter LFO but is synced by `TempoClock.h` |
| 2nd white | Crusher (insert): rate, bits, tone | TEMPO's sample-rate reducer, plus FRIZZ's own bit quantizer and tone lowpass |
| 2nd-to-last white | Delay (send): division, feedback, random, level | TEMPO's `granularDelay.h`, clocked by FRIZZ's `TempoClock.h` from MIDI clock |
| Last white | Reverb (send): decay, tone, diffusion, level | TEMPO's `reverb.h` + `fx_engine.h` |
