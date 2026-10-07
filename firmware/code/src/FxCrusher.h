/** @file FxCrusher.h
 *  @brief The crusher: sample-rate and bit reduction, tone, XOR and a press dive.
 *
 *  The XOR and the dive are ported from Bastl Instruments' Kastle 2 FX Wizard
 *  (github.com/bastl-instruments/kastle2, code/src/apps/FxWizard/), rewritten from Kastle's
 *  44kHz fixed point to 48kHz float.
 *  Copyright (c) 2024 Marek Mach, Vaclav Mach (Bastl Instruments), MIT License: see
 *  LICENSE-kastle2.
 */
#pragma once
#include "FxCommon.h"

namespace chompi
{

/** TEMPO's sample-rate reducer, plus bit-depth reduction, a tone control, and two extras
 *  from Kastle's crusher: XOR, which flips fixed bits of every 16-bit sample for a digital
 *  buzz, and a dive on every key press, the rate dropping up to 10x over 0.1s and
 *  recovering over 0.4s. Fully wet while on. The XOR's flips and the coarsest steps are a
 *  fixed size whatever the level, so a quiet signal would come out far louder than it went
 *  in: a LevelGuard (FxCommon.h) holds the output to the input's level.
 *  Params: 0 rate, 1 bits, 2 tone, 3 XOR (0 = off). */
class Crusher : public FxBase
{
public:
    enum Param
    {
        RATE,
        BITS,
        TONE,
        XOR,
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        srr_l_.Init();
        srr_r_.Init();
        xor_dc_l_.Init(sample_rate);
        xor_dc_r_.Init(sample_rate);
        dive_.Reset();
        dive_attack_inc_ = 1.f / (.1f * sample_rate);
        dive_decay_coeff_ = Decay60dBCoeff(.4f, sample_rate);

        lp_l_ = lp_r_ = 0.f;
        guard_.Init(sample_rate, 1.f);
        gate_.Init();

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        SnapParams();
    }

    void Process(float* l, float* r)
    {
        const float gate = gate_.Process();
        const float rate_knob = rate_.Process();
        const float tone_coeff = tone_coeff_.Process();

        if (gate_.TakePress())
            dive_.Press();
        const float dive = dive_.Process(dive_attack_inc_, dive_decay_coeff_);

        // Kastle: the rate divided by the dive envelope times 10, at least 1
        const float rate = rate_knob / fmaxf(1.f, dive * 10.f);
        srr_l_.SetFreq(rate);
        srr_r_.SetFreq(rate);

        // XOR before the reducer, as on Kastle. On its own XOR turns silence into a constant
        // offset, so what it adds is DC-blocked: the buzz stays, the thump on punch-in doesn't
        float xl = *l, xr = *r;
        if (xor_ > 0)
        {
            xl += xor_dc_l_.Process(Xor(xl) - xl);
            xr += xor_dc_r_.Process(Xor(xr) - xr);
        }
        else
        {
            xor_dc_l_.Process(0.f);
            xor_dc_r_.Process(0.f);
        }
        float wl = srr_l_.Process(xl);
        float wr = srr_r_.Process(xr);

        // bit-depth reduction: round to the nearest step
        const float step = step_;
        const float inv_step = inv_step_;
        wl = floorf(wl * inv_step + .5f) * step;
        wr = floorf(wr * inv_step + .5f) * step;

        // one-pole lowpass to tame the aliasing
        lp_l_ += tone_coeff * (wl - lp_l_);
        lp_r_ += tone_coeff * (wr - lp_r_);

        // never louder than what came in: the XOR's buzz and the coarsest bits are a fixed
        // size whatever the level, so on a quiet signal they'd be far over it
        float outl = lp_l_, outr = lp_r_;
        guard_.Process(*l, *r, &outl, &outr);

        *l += gate * (outl - *l);
        *r += gate * (outr - *r);
    }

    /** The parameters land at once, no slew: the randomizer's gates (FxRandomizer.h) */
    void SnapParams() override
    {
        rate_.Snap();
        tone_coeff_.Snap();
    }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case RATE:
            // TEMPO's range (SampleEngine::setSampleReducer), 21.6kHz down to 480Hz, but
            // exponential: TEMPO's is linear in Hz, which leaves the crunch in the last
            // quarter of the knob. Here every 18% of the knob halves the rate.
            rate_.target = .45f * powf(kRateBottom / .45f, val);
            break;

        case BITS:
        {
            // 16 bits down to 2, continuous so the knob sweeps smoothly
            const float bits = 16.f - val * 14.f;
            step_ = powf(2.f, 1.f - bits);
            inv_step_ = 1.f / step_;
            break;
        }

        case TONE:
        {
            // lowpass from 200Hz to 20kHz, fully open at the top
            tone_coeff_.target = ToneCoeff(val, sample_rate_);
            break;
        }

        case XOR:
        {
            // Kastle: 0 / 1000 / 2000 / 4000 over the top 30% of its Amount knob, here over
            // the whole knob
            static const float xs[] = {0.f, .233f, .667f, 1.f};
            static const float ys[] = {0.f, 1000.f, 2000.f, 4000.f};
            xor_ = static_cast<int16_t>(CurveMap(val, xs, ys, 4));
            break;
        }

        default:
            break;
        }
    }

private:
    static constexpr float kRateBottom = .01f; // 480Hz, as a fraction of 48kHz

    /** Kastle's XOR on the sample as 16-bit */
    inline float Xor(float x) const
    {
        const int16_t i = static_cast<int16_t>(fclamp(x, -1.f, 1.f) * 32767.f);
        return static_cast<float>(static_cast<int16_t>(i ^ xor_)) * (1.f / 32767.f);
    }

    float sample_rate_;
    daisysp::SampleRateReducer srr_l_, srr_r_;
    float lp_l_, lp_r_;
    Smoothed rate_;
    Smoothed tone_coeff_;
    float step_;     // quantizer step, 2^(1 - bits)
    float inv_step_; // and its inverse, for the audio callback
    int16_t xor_ = 0;
    daisysp::DcBlock xor_dc_l_, xor_dc_r_;
    LevelGuard guard_; // the output held to the input's level
    PressEnvelope dive_;
    float dive_attack_inc_, dive_decay_coeff_;
};

} // namespace chompi
