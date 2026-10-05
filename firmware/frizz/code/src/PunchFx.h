/** @file PunchFx.h
 *  @brief Punch-in effects for the white keys. Each effect is on while its key is held or
 *  latched (see NormalPage.h) and has kNumFxParams parameters, all 0..1, set by the four
 *  free knobs.
 *
 *  Two kinds:
 *   - insert (Crusher): replaces the signal while on; only the wet amount is gated.
 *   - send (DelaySend, ReverbSend): the key gates what goes into the effect, and the effect's
 *     return is added to the signal, so tails ring out after the key is released.
 *  Either way the gate slews over ~5ms so punching in and out doesn't click, and effects
 *  process every sample even while off, so engaging one never starts from stale state.
 *
 *  The engine runs them on the summed output, after the dry/wet mix and before the output
 *  gain and master compressor: crusher first, then both sends in parallel from its output
 *  (see passthroughEngine.h).
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "granularDelay.h"
#include "reverb.h"

using namespace daisysp;

namespace chompi
{

static const size_t kNumFxParams = 4;

enum FxId
{
    FX_CRUSHER,
    FX_DELAY,
    FX_REVERB,
    kNumFx,
};

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

/** TEMPO's tempo-synced delay (granularDelay.h) as a send.
 *  Params: 0 division (9 steps), 1 feedback, 2 random (bipolar, 0.5 = off), 3 level.
 *  Freeze loops the last division of the delay's output until toggled off. */
class DelaySend
{
public:
    enum Param
    {
        DIVISION,
        FEEDBACK,
        RANDOM,
        LEVEL,
    };

    static const size_t kNumDivisions = 9;

    void Init(float* buffer, float* frozen_buffer, size_t buffer_frames)
    {
        delay_.Init(buffer, frozen_buffer, buffer_frames);
        gate_ = gate_target_ = 0.f;
        level_ = level_target_ = 0.f;
        freeze_toggle_ = false;
    }

    /** Once per block: tempo, plus the clock pulses and edges that fell in this block */
    void SetTempo(int bpm) { delay_.SetTempo(bpm); }
    void ClockPulse(bool edge)
    {
        delay_.setClockPulse();
        if (edge)
            delay_.setClockEdge();
    }

    /** Feeds in_l / in_r into the delay (while on) and adds its return to *out_l / *out_r */
    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        if (freeze_toggle_)
        {
            freeze_toggle_ = false;
            delay_.toggleBufferLock();
        }

        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(level_, level_target_, .001f);

        float dl = 0.f, dr = 0.f;
        delay_.write(in_l * gate_, in_r * gate_);
        delay_.read(&dl, &dr);

        *out_l += dl * level_;
        *out_r += dr * level_;
    }

    inline void SetOn(bool on) { gate_target_ = on ? 1.f : 0.f; }

    /** From the UI: picked up by the audio callback at the next sample */
    inline void ToggleFreeze() { freeze_toggle_ = true; }
    inline bool IsFrozen() { return delay_.isFrozen(); }
    inline float GetFrozenPosition() { return delay_.getFrozenPosition(); }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case DIVISION:
            delay_.setDivision(static_cast<size_t>(val * (kNumDivisions - 1) + .5f));
            break;
        case FEEDBACK:
            delay_.setFeedback(val);
            break;
        case RANDOM:
            delay_.setRandom(val);
            break;
        case LEVEL:
            level_target_ = val;
            break;
        default:
            break;
        }
    }

private:
    granularDelay delay_;
    float gate_, gate_target_;
    float level_, level_target_;
    volatile bool freeze_toggle_;
};

/** TEMPO's reverb (reverb.h, the Rings/Clouds Griesinger topology) as a send.
 *  Params: 0 decay, 1 tone (damping), 2 diffusion, 3 level. */
class ReverbSend
{
public:
    enum Param
    {
        DECAY,
        TONE,
        DIFFUSION,
        LEVEL,
    };

    /** The reverb's 64KB buffer lives in DTCMRAM, so it's a separate static (chompi_main.cpp) */
    void Init(float sample_rate, daisysp::Reverb* reverb)
    {
        reverb_ = reverb;
        reverb_->Init(sample_rate);
        reverb_->SetAmount(1.f);    // wet only, the dry path is the signal itself
        reverb_->SetInputGain(.3f); // WAVE's / TAPE's input gain
        gate_ = gate_target_ = 0.f;
        level_ = level_target_ = 0.f;
        decay_ = decay_target_ = .5f;
        tone_ = tone_target_ = .7f;
        diffusion_ = diffusion_target_ = .625f;
    }

    /** Feeds in_l / in_r into the reverb (while on) and adds its return to *out_l / *out_r */
    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(level_, level_target_, .001f);
        fonepole(decay_, decay_target_, .001f);
        fonepole(tone_, tone_target_, .001f);
        fonepole(diffusion_, diffusion_target_, .001f);

        reverb_->SetTime(decay_);
        reverb_->SetLowpass(tone_);
        reverb_->SetDiffusion(diffusion_);

        float wl = in_l * gate_;
        float wr = in_r * gate_;
        reverb_->Process(&wl, &wr);

        *out_l += wl * level_;
        *out_r += wr * level_;
    }

    inline void SetOn(bool on) { gate_target_ = on ? 1.f : 0.f; }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case DECAY:
            // TAPE keeps reverb time within .05-.97; below .3 there's barely a tail
            decay_target_ = .3f + val * .67f;
            break;
        case TONE:
            // lowpass coefficient in the loop: dark (.3) to open (1)
            tone_target_ = .3f + val * .7f;
            break;
        case DIFFUSION:
            diffusion_target_ = .3f + val * .45f;
            break;
        case LEVEL:
            level_target_ = val;
            break;
        default:
            break;
        }
    }

private:
    daisysp::Reverb* reverb_;
    float gate_, gate_target_;
    float level_, level_target_;
    float decay_, decay_target_;
    float tone_, tone_target_;
    float diffusion_, diffusion_target_;
};

} // namespace chompi
