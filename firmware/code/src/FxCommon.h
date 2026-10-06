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

// fonepole coefficients at 48kHz. A coefficient c has a time constant of 1 / (48000 c) and
// settles (to under 1%) in about 5 of them.
// The punch-in fade: a time constant of ~5ms, settled in ~25ms
static const float kFxGateCoeff = .004f;
// The knobs' slew: a time constant of ~21ms, settled in ~100ms
static const float kFxParamCoeff = .001f;
// The slew for a moment after a scene recall (FxChain::FastSlew), the punch-in fade's, so a
// scene change is a cut rather than a sweep
static const float kFxRecallCoeff = kFxGateCoeff;

/** The slew every Smoothed uses: kFxParamCoeff, or kFxRecallCoeff right after a scene recall.
 *  A template only so the header can define the static */
template <class Unused = void>
struct FxSlewT
{
    static float coeff;
};
template <class Unused>
float FxSlewT<Unused>::coeff = kFxParamCoeff;
using FxSlew = FxSlewT<>;

/** A setting with a target, set from the UI, and a live value that follows it */
struct Smoothed
{
    float value, target;

    /** Both at v, no slew */
    void Reset(float v) { value = target = v; }
    /** The live value jumps to the target */
    void Snap() { value = target; }
    /** Once per sample, at the knobs' slew (FxSlew) */
    float Process() { return Process(FxSlew::coeff); }
    /** Once per sample, at a slew of its own */
    float Process(float coeff)
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
    /** The slewed parameters jump to their targets. The randomizer (FxRandomizer.h) sets an
     *  effect's knobs while it's silent, just before its gate opens: a gate is shorter than
     *  the knobs' slew. An effect without slewed parameters has nothing to do */
    virtual void SnapParams() {}

protected:
    FxGate gate_;
};

/** A stepped parameter's step, 0..steps - 1, from its knob value 0..1 */
inline size_t StepIndex(float val, size_t steps)
{
    return static_cast<size_t>(val * static_cast<float>(steps - 1) + .5f);
}

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

/** A one-pole lowpass's coefficient (fonepole) for a cutoff of freq Hz */
inline float OnePoleCoeff(float freq, float sample_rate)
{
    return 1.f - expf(-TWOPI_F * freq / sample_rate);
}

/** The folder's and crusher's tone knob: a lowpass from 200Hz to 20kHz, fully open at the top */
inline float ToneCoeff(float val, float sample_rate)
{
    return val >= 1.f ? 1.f : OnePoleCoeff(200.f * powf(100.f, val), sample_rate);
}

/** The envelope a press starts (the crusher's dive, the shifter's swoop, the slicer's steps):
 *  a linear attack up to 1, then an exponential decay */
struct PressEnvelope
{
    float value = 0.f;
    bool attacking = false;

    void Reset()
    {
        value = 0.f;
        attacking = false;
    }
    inline void Press() { attacking = true; }
    /** Once per sample */
    float Process(float attack_inc, float decay_coeff)
    {
        if (attacking)
        {
            value += attack_inc;
            if (value >= 1.f)
            {
                value = 1.f;
                attacking = false;
            }
        }
        else
            value *= decay_coeff;
        return value;
    }
};

/** A stereo delay line of N frames (a power of 2): write one frame a sample, read behind it */
template <size_t N>
struct StereoRing
{
    static_assert((N & (N - 1)) == 0, "a power of 2");
    static const size_t kMask = N - 1;

    void Clear()
    {
        for (size_t c = 0; c < 2; c++)
            for (size_t i = 0; i < N; i++)
                buf[c][i] = 0.f;
        pos = 0;
    }
    /** delay frames behind the last frame written, interpolated */
    inline float Read(size_t c, float delay) const { return ReadFrac(buf[c], kMask, pos - 1, delay); }
    /** This sample's frame, one channel at a time; Advance once both are written */
    inline void Write(size_t c, float x) { buf[c][pos] = x; }
    inline void Advance() { pos = (pos + 1) & kMask; }
    /** The index of the last frame written, for reads of their own (& kMask) */
    inline size_t Last() const { return pos - 1; }

    float buf[2][N];
    size_t pos;
};

} // namespace chompi
