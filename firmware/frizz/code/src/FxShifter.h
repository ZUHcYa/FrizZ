/** @file FxShifter.h
 *  @brief The shifter, a one-tap delay-line pitch shifter.
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

// Shifter map: LFO rate in Hz by the shift knob, 1Hz at the centre, 100Hz at the bottom
// (down), 260Hz at the top (up)
static const float kShifterRateX[] = {0.f, .35f, .5f, .65f, 1.f};
static const float kShifterRateHz[] = {100.f, 1.2f, 1.f, 1.2f, 260.f};

/** Kastle's shifter, a one-tap delay-line pitch shifter: an LFO sweeps the delay
 *  across 11.6ms as a ramp, and the tap fades out and in around each wrap (Kastle's 64
 *  samples at 44kHz). Shift's distance from centre sets the LFO rate, its side the direction:
 *  right of centre up, left down, the centre itself dry. Near the centre that's a slight
 *  detune, further out a shift of several semitones, and towards the ends the fades turn it
 *  into a buzzing ring-mod-like tone, at up to 100Hz down and 260Hz up, as on Kastle.
 *  Swoop is Kastle's trigger envelope on the rate (0.1s up, 1s down), fired by the key
 *  press, up to 8x down and 20x up; Kastle tied its depth to the shift knob. Feedback recirculates the
 *  shifted output into the delay, so the shift spirals (Kastle: its comb around every mode).
 *  Params: 0 shift (bipolar, 0.5 = off), 1 swoop, 2 feedback, 3 stereo. */
class Shifter : public FxBase
{
public:
    enum Param
    {
        SHIFT,
        SWOOP,
        FEEDBACK,
        STEREO,
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        for (size_t c = 0; c < 2; c++)
        {
            for (size_t i = 0; i < kBufSize; i++)
                buf_[c][i] = 0.f;
            phase_[c] = 0.f;
        }
        write_pos_ = 0;
        gate_.Init();
        env_ = 0.f;
        env_attacking_ = false;
        env_attack_inc_ = 1.f / (.1f * sample_rate);
        env_decay_coeff_ = expf(-6.9078f / sample_rate); // 1s to -60dB

        SetParam(SHIFT, .5f);
        for (size_t i = 1; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        dry_.Snap();
        feedback_.Snap();
    }

    void Process(float* l, float* r)
    {
        const float gate = gate_.Process();
        const float dry = dry_.Process(.002f);
        const float feedback = feedback_.Process();

        if (gate_.TakePress())
            env_attacking_ = true;
        if (env_attacking_)
        {
            env_ += env_attack_inc_;
            if (env_ >= 1.f)
            {
                env_ = 1.f;
                env_attacking_ = false;
            }
        }
        else
            env_ *= env_decay_coeff_;
        // Kastle: the rate times the envelope's swoop, at least 1x, up to 8x down, 20x up
        const float mult = fmaxf(1.f, env_ * swoop_ * (up_ ? 20.f : 8.f));

        float* const io[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            // capped so the cycle stays a few samples long
            const float inc = fminf(rate_[c] * mult / sample_rate_, .25f);
            phase_[c] += inc;
            if (phase_[c] >= 1.f)
                phase_[c] -= 1.f;

            // up: the delay shrinks across the cycle, so the tap reads faster
            const float ramp = up_ ? 1.f - phase_[c] : phase_[c];
            const float wet_raw = ReadFrac(buf_[c], kBufMask, write_pos_ - 1, 1.f + ramp * kSweepFrames);

            // fade out and in around the wrap, over kFadeFrames each side
            const float fade_phase = kFadeFrames * inc;
            float fade = 1.f;
            if (phase_[c] < fade_phase)
                fade = phase_[c] / fade_phase;
            else if (1.f - phase_[c] < fade_phase)
                fade = (1.f - phase_[c]) / fade_phase;
            const float wet = wet_raw * fade;

            buf_[c][write_pos_] = SoftClip(*io[c] + wet * feedback);

            const float out = wet + dry * (*io[c] - wet);
            *io[c] += gate * (out - *io[c]);
        }
        write_pos_ = (write_pos_ + 1) & kBufMask;
    }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case SHIFT:
        {
            shift_ = val;
            up_ = val > .5f;
            // Kastle: dry in .48-.52, fading to fully wet by .47 / .53
            const float d = fabsf(val - .5f);
            dry_.target = fclamp((.03f - d) / .01f, 0.f, 1.f);
            break;
        }
        case SWOOP:
            swoop_ = val;
            break;
        case FEEDBACK:
            feedback_.target = val * .45f; // Kastle's comb: up to .35, plus its input boost
            break;
        case STEREO:
            stereo_hz_ = val * 20.f; // Kastle: the right LFO up to 20Hz faster
            break;
        default:
            break;
        }
        const float base = CurveMap(shift_, kShifterRateX, kShifterRateHz, 5);
        rate_[0] = base;
        rate_[1] = base + stereo_hz_;
    }

private:
    static const size_t kBufSize = 1024;
    static const size_t kBufMask = kBufSize - 1;
    static constexpr float kSweepFrames = 557.f; // Kastle's 511 at 44kHz, 11.6ms
    static constexpr float kFadeFrames = 70.f;   // Kastle's 64 at 44kHz

    float sample_rate_;
    float buf_[2][kBufSize];
    size_t write_pos_;
    float phase_[2];
    float rate_[2] = {1.f, 1.f};
    float shift_ = .5f;
    bool up_ = false;
    float swoop_ = 0.f;
    float stereo_hz_ = 0.f;
    Smoothed dry_;
    Smoothed feedback_;
    float env_, env_attack_inc_, env_decay_coeff_;
    bool env_attacking_;
};

} // namespace chompi
