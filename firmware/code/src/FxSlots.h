/** @file FxSlots.h
 *  @brief The punch-in FX keys: one entry per FxId (FxChain.h), in the same order, with the
 *  key, LED and colours the play page (NormalPage.h) gives each. Their knobs (defaults, steps,
 *  coarse grids) are in FxParams.h. The controls are described in MANUAL.md.
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
};

static const FxSlot kFxSlots[] = {
    {Hardware::SwId::KEY_1, 24, purple, {purple, white, med_blue}, FxKind::INSERT},   // freezer
    // the shift knob's LED: blue down, white off, red up
    {Hardware::SwId::KEY_2, 23, red, {blue, white, red}, FxKind::INSERT},             // shifter
    {Hardware::SwId::KEY_3, 22, magenta, {magenta, white, orange}, FxKind::INSERT},   // folder
    {Hardware::SwId::KEY_4, 21, orange, {yellow, orange, red}, FxKind::INSERT},       // crusher
    {Hardware::SwId::KEY_5, 20, pink, {pink, white, med_blue}, FxKind::INSERT},       // filter
    {Hardware::SwId::KEY_6, 19, med_blue, {med_blue, white, purple}, FxKind::INSERT}, // flanger
    {Hardware::SwId::KEY_7, 18, lime, {orange, white, med_blue}, FxKind::LOOP},       // resonator
    {Hardware::SwId::KEY_8, 17, yellow, {yellow, white, green}, FxKind::INSERT},      // slicer
    {Hardware::SwId::KEY_14, 11, green, {green, white, med_blue}, FxKind::SEND},      // delay
    {Hardware::SwId::KEY_15, 10, blue, {med_blue, blue, purple}, FxKind::SEND},       // reverb
};
static_assert(sizeof(kFxSlots) / sizeof(kFxSlots[0]) == kNumFx, "one per FxId");

} // namespace chompi
