/** @file FxParams.h
 *  @brief The punch-in FX's knobs: one entry per FxId (FxChain.h), in the same order, with how
 *  many knobs each uses, their defaults, which are stepped and their coarse grids for SHIFT +
 *  turn. No hardware here, so the play page's logic (FxControls.h) can be tested on the host.
 *  The keys, LEDs and colours that go with each FX are in FxSlots.h.
 *
 *  Every effect's knobs follow one pattern, so they're guessable without labels: knob 1 is the
 *  main control, 2 feedback (or resonance), 3 tone or colour, 4 stereo or level. An effect
 *  without one of these puts its odd parameter there.
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
    float defaults[kNumFxParams]; // audible from the first press; stepped ones on their grid
    uint8_t steps[kNumFxParams];  // stepped parameters' number of steps, 0 = continuous
    FxGrid coarse[kNumFxParams];  // SHIFT + turn
};

static const FxParams kFxParams[] = {
    // freezer: length (1/8), feedback (pure repeat), roll (off), stereo (off)
    {4, {2.f / 7.f, 0.f, 0.f, 0.f}, {Freezer::kNumLengths, 0, Freezer::kNumRolls, 0},
     {StepGrid(Freezer::kNumLengths), kGrid10, StepGrid(Freezer::kNumRolls), kGrid10}},
    // shifter: shift (+7 semitones, a fifth), feedback (off), swoop (off), stereo (off).
    // Coarse stereo: quarter semitones
    {4, {19.f / 24.f, 0.f, 0.f, 0.f}, {Shifter::kNumShifts, 0, 0, 0},
     {{0.f, 1.f, kShiftPoints, sizeof(kShiftPoints) / sizeof(kShiftPoints[0])}, kGrid10, kGrid10,
      {0.f, .25f, nullptr, 0}}},
    // folder: drive (5.7x), shape (sine), tone (open), symmetry (off: odd harmonics only).
    // Coarse drive: doublings, 1x to 32x
    {4, {.5f, 0.f, 1.f, 0.f}, {0, 0, 0, 0},
     {{0.f, .2f, nullptr, 0}, kGrid10, kGridTone, kGrid10}},
    // crusher: rate (8.6kHz), bits, tone, XOR (off). Coarse rate: 48kHz / 4, 8 ... 64, so
    // 12k, 6k, 3k, 1.5k, 750Hz; coarse bits: whole bits
    {4, {.24f, .5f, 1.f, 0.f}, {0, 0, 0, 0},
     {{.154410f, .182088f, nullptr, 0}, {0.f, 1.f / 14.f, nullptr, 0}, kGridTone, kGrid10}},
    // filter: cutoff (lowpass), resonance, LFO depth (off), LFO division (1 bar). The cutoff
    // is a DJ filter's (lowpass below the centre, highpass above), not in Hz, so coarse is 10%
    {4, {.3f, .5f, 0.f, .6667f}, {0, 0, 0, Filter::kNumLfoDivisions},
     {kGrid10, kGrid10, kGrid10, StepGrid(Filter::kNumLfoDivisions)}},
    // flanger: rate (.55Hz), feedback, amount (half), stereo (off)
    {4, {.45f, .5f, .5f, 0.f}, {0, 0, 0, 0},
     {kGrid10, kGrid10, kGrid10, kGrid10}},
    // resonator: pitch (110Hz), feedback, tone (6.6kHz), stereo (off). Coarse pitch: the
    // notes at A440, F#0 to A5; coarse tone: octaves down from 15kHz
    {4, {.4364f, .7f, .7f, 0.f}, {0, 0, 0, 0},
     {{.436295f, .0156585f, nullptr, 0}, kGrid10, {1.f, .255958f, nullptr, 0}, kGrid10}},
    // slicer: pattern (x..x..x.), decay (100ms), chance (off), stereo (off)
    {4, {4.f / 7.f, .5f, 0.f, 0.f}, {Slicer::kNumPatterns, 0, 0, Slicer::kNumPatterns},
     {StepGrid(Slicer::kNumPatterns), kGrid10, kGrid10, StepGrid(Slicer::kNumPatterns)}},
    // delay: division (1/4), feedback, random (off), level
    {4, {.25f, .4f, .5f, .7f}, {DelaySend::kNumDivisions, 0, 0, 0},
     {StepGrid(DelaySend::kNumDivisions), kGrid10, kGrid10, kGrid10}},
    // reverb: decay, diffusion, tone, level
    {4, {.6f, .6f, .6f, .7f}, {0, 0, 0, 0},
     {kGrid10, kGrid10, kGrid10, kGrid10}},
};
static_assert(sizeof(kFxParams) / sizeof(kFxParams[0]) == kNumFx, "one per FxId");

} // namespace chompi
