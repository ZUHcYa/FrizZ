/** @file FxSlots.h
 *  @brief The punch-in FX keys: one entry per FxId (FxChain.h), in the same order, with
 *  everything the play page (NormalPage.h) needs for each. The controls are described in
 *  the FRIZZ README.
 */
#pragma once
#include "FxChain.h"
#include "hardware.h"
#include "LedColors.h"

namespace chompi
{

enum class FxKind
{
    INSERT, // replaces the signal while on
    SEND,   // adds its return; the key LED also glows with the tail after release
    LOOP,   // a feedback loop around some of the inserts (the resonator)
};

struct FxSlot
{
    Hardware::SwId key;
    uint8_t key_led;              // SMT LED, TestPage's led_map
    const float* key_color;
    const float* knob_colors[3];  // the knob LEDs at 0 / .5 / 1
    FxKind kind;
    uint8_t num_params;           // knobs used, the first num_params of the four
    float defaults[kNumFxParams]; // audible from the first press; stepped ones on their grid
    uint8_t steps[kNumFxParams];  // stepped parameters' number of steps, 0 = continuous
};

static const FxSlot kFxSlots[] = {
    // freezer: length (1/8), feedback (pure repeat), stereo (off), roll (off)
    {Hardware::SwId::KEY_1, 24, purple, {purple, white, med_blue}, FxKind::INSERT, 4,
     {2.f / 7.f, 0.f, 0.f, 0.f}, {Freezer::kNumLengths, 0, 0, Freezer::kNumRolls}},
    // shifter: shift (+7 semitones, a fifth), swoop (off), feedback (off), stereo (off).
    // The shift knob's LED: blue down, white off, red up
    {Hardware::SwId::KEY_2, 23, red, {blue, white, red}, FxKind::INSERT, 4,
     {19.f / 24.f, 0.f, 0.f, 0.f}, {Shifter::kNumShifts, 0, 0, 0}},
    // folder: drive (5.7x), shape (sine), symmetry (off: odd harmonics only), tone (open)
    {Hardware::SwId::KEY_3, 22, magenta, {magenta, white, orange}, FxKind::INSERT, 4,
     {.5f, 0.f, 0.f, 1.f}, {0, 0, 0, 0}},
    // crusher: rate (8.6kHz), bits, tone, XOR (off)
    {Hardware::SwId::KEY_4, 21, orange, {yellow, orange, red}, FxKind::INSERT, 4,
     {.24f, .5f, 1.f, 0.f}, {0, 0, 0, 0}},
    // filter: cutoff (lowpass), resonance, LFO depth (off), LFO division (1 bar)
    {Hardware::SwId::KEY_5, 20, pink, {pink, white, med_blue}, FxKind::INSERT, 4,
     {.3f, .5f, 0.f, .6667f}, {0, 0, 0, Filter::kNumLfoDivisions}},
    // flanger: rate (.55Hz), amount (half), feedback, stereo (off)
    {Hardware::SwId::KEY_6, 19, med_blue, {med_blue, white, purple}, FxKind::INSERT, 4,
     {.45f, .5f, .5f, 0.f}, {0, 0, 0, 0}},
    // resonator: pitch (110Hz), feedback, tone (6.6kHz), stereo (off)
    {Hardware::SwId::KEY_7, 18, lime, {orange, white, med_blue}, FxKind::LOOP, 4,
     {.4364f, .7f, .7f, 0.f}, {0, 0, 0, 0}},
    // slicer: pattern (x..x..x.), decay (100ms), chance (off), stereo (off)
    {Hardware::SwId::KEY_8, 17, yellow, {yellow, white, green}, FxKind::INSERT, 4,
     {4.f / 7.f, .5f, 0.f, 0.f}, {Slicer::kNumPatterns, 0, 0, Slicer::kNumPatterns}},
    // delay: division (1/4), feedback, random (off), level
    {Hardware::SwId::KEY_14, 11, green, {green, white, med_blue}, FxKind::SEND, 4,
     {.25f, .4f, .5f, .7f}, {DelaySend::kNumDivisions, 0, 0, 0}},
    // reverb: decay, tone, diffusion, level
    {Hardware::SwId::KEY_15, 10, blue, {med_blue, blue, purple}, FxKind::SEND, 4,
     {.6f, .6f, .6f, .7f}, {0, 0, 0, 0}},
};
static_assert(sizeof(kFxSlots) / sizeof(kFxSlots[0]) == kNumFx, "one per FxId");

} // namespace chompi
