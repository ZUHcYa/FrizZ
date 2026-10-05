/** @file FxCommon.h
 *  @brief What the punch-in effects (Fx*.h) share.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"

using namespace daisysp;

namespace chompi
{

// Parameters per effect, one per knob
static const size_t kNumFxParams = 4;

// The punch-in fade: fonepole's coefficient, ~5ms at 48kHz
static const float kFxGateCoeff = .004f;
// The knobs' slew, ~20ms at 48kHz
static const float kFxParamCoeff = .001f;

/** A setting with a target, set from the UI, and a live value that follows it */
struct Smoothed
{
    float value, target;

    /** Both at v, no slew */
    void Reset(float v) { value = target = v; }
    /** The live value jumps to the target */
    void Snap() { value = target; }
    /** Once per sample */
    float Process(float coeff = kFxParamCoeff)
    {
        fonepole(value, target, coeff);
        return value;
    }
};

/** An effect's key: on while held or latched, faded in and out over ~5ms so punching in
 *  doesn't click, and the press itself for the effects that react to it */
class FxGate
{
public:
    void Init()
    {
        value_ = target_ = 0.f;
        on_ = false;
        pressed_ = false;
    }

    /** From the UI. Returns true on a press (off to on) */
    bool SetOn(bool on)
    {
        const bool press = on && !on_;
        on_ = on;
        if (press)
            pressed_ = true;
        target_ = on ? 1.f : 0.f;
        return press;
    }

    /** Once per sample, before the gate is used: the fade, 0..1 */
    float Process()
    {
        fonepole(value_, target_, kFxGateCoeff);
        return value_;
    }

    inline float Value() const { return value_; }
    inline bool IsOn() const { return on_; }

    /** True once after each press, for the audio callback */
    bool TakePress()
    {
        if (!pressed_)
            return false;
        pressed_ = false;
        return true;
    }

private:
    float value_, target_;
    volatile bool on_;
    volatile bool pressed_;
};

/** What every punch-in effect has: a key and kNumFxParams parameters, each 0..1. Process is
 *  each effect's own, called by FxChain in its place in the chain. Effects process every
 *  sample even while off, so engaging one never starts from stale state. */
class FxBase
{
public:
    virtual void SetOn(bool on) { gate_.SetOn(on); }
    virtual void SetParam(size_t param, float val) = 0;

protected:
    FxGate gate_;
};

/** Kastle's curve_map: linear between (xs[i], ys[i]) points, clamped at both ends */
inline float CurveMap(float x, const float* xs, const float* ys, size_t n)
{
    if (x <= xs[0])
        return ys[0];
    for (size_t i = 1; i < n; i++)
    {
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    }
    return ys[n - 1];
}

/** Linear-interpolated read, delay in frames behind the last write, from a power-of-2 ring */
inline float ReadFrac(const float* buf, size_t mask, size_t write_pos, float delay)
{
    const size_t whole = static_cast<size_t>(delay);
    const float frac = delay - static_cast<float>(whole);
    const float a = buf[(write_pos - whole) & mask];
    const float b = buf[(write_pos - whole - 1) & mask];
    return a + (b - a) * frac;
}

} // namespace chompi
