/** @file FxParams.h
 *  @brief The punch-in FX's knobs: one entry per FxId (FxChain.h), in the same order, with how
 *  many knobs each uses, their defaults, which are stepped and their coarse grids for SHIFT +
 *  turn. No hardware here, so the play page's logic (FxControls.h) can be tested on the host.
 *  The keys, LEDs and colours that go with each FX are in FxSlots.h.
 *
 *  Every effect's knobs follow one pattern, so they're guessable without labels: knob 1 is the
 *  main control, 2 feedback (or resonance), 3 tone or colour, 4 stereo or level. An effect
 *  without one of these puts its odd parameter there.
 *
 *  The defaults leave every effect silent or nearly so: the knob that brings it in (shift,
 *  drive, rate and bits, cutoff, amount, feedback, level) starts neutral, the others where
 *  they sound good, so one turn brings the effect in. The freezer and slicer can't be
 *  neutral: they start at their most transparent setting.
 */
#pragma once
#include "FxChain.h"

namespace chompi
{

/** A parameter's coarse grid, for SHIFT + turn: the points origin + k * spacing that lie in
 *  0-1, or, if points is set, that list in ascending order instead */
struct FxGrid
{
    float origin;
    float spacing;
    const float* points;
    uint8_t num_points;
};

// 10% steps, for parameters without musical values
static const FxGrid kGrid10 = {0.f, .1f, nullptr, 0};
// one step of a stepped parameter
static constexpr FxGrid StepGrid(size_t steps) { return {0.f, 1.f / (steps - 1), nullptr, 0}; }
// octaves of a tone lowpass, 200Hz * 100^val, counted down from fully open at the top:
// 20k, 10k, 5k ... 312Hz
static const FxGrid kGridTone = {1.f, .150515f, nullptr, 0};
// shifter shift (-12..+12 over 0-1): octaves, fifths and fourths
static const float kShiftPoints[] = {0.f, 5.f / 24.f, 7.f / 24.f, .5f, 17.f / 24.f, 19.f / 24.f, 1.f};

struct FxParams
{
    uint8_t num_params;           // knobs used, the first num_params of the four
    float defaults[kNumFxParams]; // the main knob neutral, see above; stepped ones on their grid
    uint8_t steps[kNumFxParams];  // stepped parameters' number of steps, 0 = continuous
    FxGrid coarse[kNumFxParams];  // SHIFT + turn
    uint8_t fade;                 // bit knob: the knobs that leave the effect neutral at
                                  // their defaults, whatever the others say. A scene morph
                                  // fades the effect in and out with them (FxMorph.h); 0: it
                                  // can't be faded
};

static const FxParams kFxParams[] = {
    // freezer: length (1 bar: a tap shorter than that hears the live signal), feedback (pure
    // repeat), roll (off), stereo (off)
    {4, {1.f, 0.f, 0.f, 0.f}, {Freezer::kNumLengths, 0, Freezer::kNumRolls, 0},
     {StepGrid(Freezer::kNumLengths), kGrid10, StepGrid(Freezer::kNumRolls), kGrid10}, 0},
    // shifter: shift (0, off), feedback (off), swoop (off), stereo (off).
    // Coarse stereo: quarter semitones
    {4, {.5f, 0.f, 0.f, 0.f}, {Shifter::kNumShifts, 0, 0, 0},
     {{0.f, 1.f, kShiftPoints, sizeof(kShiftPoints) / sizeof(kShiftPoints[0])}, kGrid10, kGrid10,
      {0.f, .25f, nullptr, 0}}, 0},
    // folder: drive (1x, barely folding), shape (sine), tone (open), symmetry (off: odd
    // harmonics only). Coarse drive: doublings, 1x to 32x
    {4, {0.f, 0.f, 1.f, 0.f}, {0, 0, 0, 0},
     {{0.f, .2f, nullptr, 0}, kGrid10, kGridTone, kGrid10}, 0xf},
    // crusher: rate (21.6kHz), bits (16), tone (open), XOR (off). Coarse rate: 48kHz / 4, 8
    // ... 64, so 12k, 6k, 3k, 1.5k, 750Hz; coarse bits: whole bits
    {4, {0.f, 0.f, 1.f, 0.f}, {0, 0, 0, 0},
     {{.154410f, .182088f, nullptr, 0}, {0.f, 1.f / 14.f, nullptr, 0}, kGridTone, kGrid10}, 0xf},
    // filter: cutoff (centre, flat), resonance, LFO depth (off), LFO division (1 bar). The
    // cutoff is a DJ filter's (lowpass below the centre, highpass above), not in Hz, so
    // coarse is 10%
    {4, {.5f, .5f, 0.f, .6667f}, {0, 0, 0, Filter::kNumLfoDivisions},
     {kGrid10, kGrid10, kGrid10, StepGrid(Filter::kNumLfoDivisions)}, 0x7},
    // flanger: rate (.55Hz), feedback, amount (off: dry), stereo (off)
    {4, {.45f, .5f, 0.f, 0.f}, {0, 0, 0, 0},
     {kGrid10, kGrid10, kGrid10, kGrid10}, 0x4},
    // resonator: pitch (110Hz), feedback (off), tone (6.6kHz), stereo (off). Coarse pitch:
    // the notes at A440, F#0 to A5; coarse tone: octaves down from 15kHz
    {4, {.4364f, 0.f, .7f, 0.f}, {0, 0, 0, 0},
     {{.436295f, .0156585f, nullptr, 0}, kGrid10, {1.f, .255958f, nullptr, 0}, kGrid10}, 0x2},
    // slicer: pattern (xxxxxxxx), decay (1s: a slight pump), chance (off), stereo (off)
    {4, {1.f, 1.f, 0.f, 0.f}, {Slicer::kNumPatterns, 0, 0, Slicer::kNumPatterns},
     {StepGrid(Slicer::kNumPatterns), kGrid10, kGrid10, StepGrid(Slicer::kNumPatterns)}, 0},
    // delay: division (1/4), feedback, random (off), level (off)
    {4, {.25f, .4f, .5f, 0.f}, {DelaySend::kNumDivisions, 0, 0, 0},
     {StepGrid(DelaySend::kNumDivisions), kGrid10, kGrid10, kGrid10}, 0x8},
    // reverb: decay, diffusion, tone, level (off)
    {4, {.6f, .6f, .6f, 0.f}, {0, 0, 0, 0},
     {kGrid10, kGrid10, kGrid10, kGrid10}, 0x8},
};
static_assert(sizeof(kFxParams) / sizeof(kFxParams[0]) == kNumFx, "one per FxId");

} // namespace chompi
