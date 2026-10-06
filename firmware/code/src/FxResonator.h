/** @file FxResonator.h
 *  @brief The resonator, a comb feedback loop around the inserts.
 *
 *  Ported from Bastl Instruments' Kastle 2 FX Wizard (github.com/bastl-instruments/kastle2,
 *  code/src/apps/FxWizard/), rewritten from Kastle's 44kHz fixed point to 48kHz float.
 *  Copyright (c) 2024 Marek Mach, Vaclav Mach (Bastl Instruments), MIT License: see
 *  LICENSE-kastle2.
 */
#pragma once
#include "FxCommon.h"

namespace chompi
{

/** The feedback comb Kastle runs around every FX Wizard mode, as its own key. While
 *  on, the loop wraps the inserts between Feed and Tap (FxChain.h), so the ringing goes
 *  through every one of them that's on, on every trip. In the loop, as on Kastle:
 *  a soft clipper, a lowpass and a 50Hz highpass, which keep it bounded whatever is inside.
 *  On its own it's a comb on the dry signal. Kastle's comb is 100-2000 samples at 44kHz and
 *  at most about 40% feedback; this one is tunable and goes up to 98%.
 *  Params: 0 pitch (22-880Hz), 1 feedback, 2 tone (the loop's lowpass), 3 stereo (the right
 *  channel up to 12 semitones higher). */
class Resonator : public FxBase
{
public:
    enum Param
    {
        PITCH,
        FEEDBACK,
        TONE,
        STEREO,
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        ring_.Clear();
        for (size_t c = 0; c < 2; c++)
        {
            lp_[c] = 0.f;
            hp_[c].Init(sample_rate);
            ret_[c] = 0.f;
        }
        gate_.Init();

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        for (size_t c = 0; c < 2; c++)
            delay_[c].Snap();
        feedback_.Snap();
        lp_coeff_.Snap();
    }

    /** After the freezer: adds the loop's return, scaling the input down as Kastle does */
    void Feed(float* l, float* r)
    {
        const float gate = gate_.Process();
        const float feedback = feedback_.Process();
        lp_coeff_.Process();

        const float fb = gate * feedback;
        float* const io[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            const float delay = delay_[c].Process();
            ret_[c] = fb * ring_.Read(c, delay - 1.f);
            *io[c] = *io[c] * (1.f - .5f * fb) + ret_[c];
        }
    }

    /** After the flanger: into the loop through the clipper and filters */
    void Tap(float l, float r)
    {
        const float in[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            lp_[c] += lp_coeff_.value * (SoftClip(in[c]) - lp_[c]);
            ring_.Write(c, hp_[c].Process(lp_[c]));
        }
        ring_.Advance();
    }

    /** What the loop added in the last Feed, for the key LED */
    inline float Return() const { return ret_[0] + ret_[1]; }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case PITCH:
            pitch_hz_ = kLowestHz * powf(kHighestHz / kLowestHz, val);
            break;
        case FEEDBACK:
            feedback_.target = val * .98f;
            break;
        case TONE:
        {
            // the loop's lowpass, 1kHz to Kastle's 15kHz
            const float freq = 1000.f * powf(15.f, val);
            lp_coeff_.target = OnePoleCoeff(freq, sample_rate_);
            break;
        }
        case STEREO:
            stereo_ = val;
            break;
        default:
            break;
        }
        delay_[0].target = sample_rate_ / pitch_hz_;
        delay_[1].target = sample_rate_ / (pitch_hz_ * powf(2.f, stereo_));
    }

private:
    static const size_t kBufSize = 4096; // > 48kHz / 22Hz
    static constexpr float kLowestHz = 22.f;   // Kastle's 2000 samples at 44kHz
    static constexpr float kHighestHz = 880.f; // Kastle stops at 440Hz

    /** The loop's highpass: a one-pole at 50Hz, Kastle's */
    struct Highpass
    {
        void Init(float sample_rate)
        {
            coeff = OnePoleCoeff(50.f, sample_rate);
            lp = 0.f;
        }
        float Process(float x)
        {
            lp += coeff * (x - lp);
            return x - lp;
        }
        float coeff, lp;
    };

    float sample_rate_;
    StereoRing<kBufSize> ring_;
    float lp_[2];
    Highpass hp_[2];
    float ret_[2];
    Smoothed feedback_;
    Smoothed lp_coeff_; // set in Feed, used in Tap
    Smoothed delay_[2]; // frames, per channel
    float pitch_hz_ = kLowestHz;
    float stereo_ = 0.f;
};

} // namespace chompi
