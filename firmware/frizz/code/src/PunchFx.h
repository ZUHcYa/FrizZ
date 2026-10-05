/** @file PunchFx.h
 *  @brief Punch-in effects for the white keys. Each effect is on while its key is held or
 *  latched (see NormalPage.h) and has kNumFxParams parameters, all 0..1, set by the four
 *  free knobs.
 *
 *  Effects process every sample even while off, so engaging one never starts from stale
 *  state; only the wet amount is gated, with a ~5ms slew so punching in and out doesn't
 *  click.
 *
 *  The engine runs them on the summed output, after the dry/wet mix and before the output
 *  gain and master compressor (see passthroughEngine.h).
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"

using namespace daisysp;

namespace chompi
{

static const size_t kNumFxParams = 4;

// ~5ms at 48kHz
static const float kFxGateCoeff = .004f;

/** KEY_1: TEMPO's sample-rate reducer, plus bit-depth reduction, a tone control and mix.
 *  Params: 0 rate, 1 bits, 2 tone, 3 mix. */
class Crusher
{
public:
    enum Param
    {
        RATE,
        BITS,
        TONE,
        MIX,
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        srr_l_.Init();
        srr_r_.Init();

        lp_l_ = lp_r_ = 0.f;
        gate_ = gate_target_ = 0.f;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        rate_ = rate_target_;
        mix_ = mix_target_;
        tone_coeff_ = tone_coeff_target_;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(mix_, mix_target_, .001f);
        fonepole(rate_, rate_target_, .001f);
        fonepole(tone_coeff_, tone_coeff_target_, .001f);

        srr_l_.SetFreq(rate_);
        srr_r_.SetFreq(rate_);
        float wl = srr_l_.Process(*l);
        float wr = srr_r_.Process(*r);

        // bit-depth reduction: round to the nearest step
        const float step = step_;
        const float inv_step = 1.f / step;
        wl = floorf(wl * inv_step + .5f) * step;
        wr = floorf(wr * inv_step + .5f) * step;

        // one-pole lowpass to tame the aliasing
        lp_l_ += tone_coeff_ * (wl - lp_l_);
        lp_r_ += tone_coeff_ * (wr - lp_r_);

        const float amt = gate_ * mix_;
        *l += amt * (lp_l_ - *l);
        *r += amt * (lp_r_ - *r);
    }

    inline void SetOn(bool on) { gate_target_ = on ? 1.f : 0.f; }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case RATE:
            // TEMPO's mapping (SampleEngine::setSampleReducer): 21.6kHz down to 480Hz
            rate_target_ = fclamp((1.f - val) * .45f, .01f, 1.f);
            break;

        case BITS:
        {
            // 16 bits down to 2, continuous so the knob sweeps smoothly
            const float bits = 16.f - val * 14.f;
            step_ = powf(2.f, 1.f - bits);
            break;
        }

        case MIX:
            mix_target_ = val;
            break;

        case TONE:
        {
            // lowpass from 200Hz to 20kHz, fully open at the top
            if (val >= 1.f)
                tone_coeff_target_ = 1.f;
            else
            {
                const float freq = 200.f * powf(100.f, val);
                tone_coeff_target_ = 1.f - expf(-TWOPI_F * freq / sample_rate_);
            }
            break;
        }

        default:
            break;
        }
    }

private:
    float sample_rate_;
    daisysp::SampleRateReducer srr_l_, srr_r_;
    float lp_l_, lp_r_;
    float gate_, gate_target_;
    float rate_, rate_target_;
    float mix_, mix_target_;
    float tone_coeff_, tone_coeff_target_;
    float step_; // quantizer step, 2^(1 - bits)
};

} // namespace chompi
