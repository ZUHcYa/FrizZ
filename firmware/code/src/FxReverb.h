/** @file FxReverb.h
 *  @brief TEMPO's reverb as a send.
 */
#pragma once
#include "FxCommon.h"
#include "reverb.h"

namespace chompi
{

/** TEMPO's reverb (reverb.h, the Rings/Clouds Griesinger topology) as a send.
 *  Params: 0 decay, 1 diffusion, 2 tone (damping), 3 level. */
class ReverbSend : public FxBase
{
public:
    enum Param
    {
        DECAY,
        DIFFUSION,
        TONE,
        LEVEL,
    };

    /** The reverb's 64KB buffer lives in DTCMRAM, so it's a separate static (chompi_main.cpp) */
    void Init(float sample_rate, daisysp::Reverb* reverb)
    {
        reverb_ = reverb;
        reverb_->Init(sample_rate);
        reverb_->SetAmount(1.f);    // wet only, the dry path is the signal itself
        reverb_->SetInputGain(.3f); // WAVE's / TAPE's input gain
        gate_.Init();
        level_.Reset(0.f);
        decay_.Reset(.5f);
        tone_.Reset(.7f);
        diffusion_.Reset(.625f);
    }

    /** Feeds in_l / in_r into the reverb (while on) and adds its return to *out_l / *out_r */
    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        const float gate = gate_.Process();
        const bool silent_in = gate_.Asleep();
        if (tail_.Sleeping(silent_in, kSleepSamples))
            return;
        const float level = level_.Process();

        reverb_->SetTime(decay_.Process());
        reverb_->SetLowpass(tone_.Process());
        reverb_->SetDiffusion(diffusion_.Process());

        float wl = in_l * gate;
        float wr = in_r * gate;
        reverb_->Process(&wl, &wr);
        tail_.Track(silent_in, wl, wr);

        *out_l += wl * level;
        *out_r += wr * level;
    }

    /** Off and its tail rung out: nothing to add, and its meter isn't needed */
    inline bool Sleeping() const { return tail_.quiet >= kSleepSamples; }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case DECAY:
            // TAPE keeps reverb time within .05-.97; below .3 there's barely a tail
            decay_.target = .3f + val * .67f;
            break;
        case TONE:
            // lowpass coefficient in the loop: dark (.3) to open (1)
            tone_.target = .3f + val * .7f;
            break;
        case DIFFUSION:
            diffusion_.target = .3f + val * .45f;
            break;
        case LEVEL:
            level_.target = val;
            break;
        default:
            break;
        }
    }

private:
    // 2s: far longer than the reverb's delay lines, so nothing under -120dB can come back
    static const uint32_t kSleepSamples = 96000;
    daisysp::Reverb* reverb_;
    TailWatch tail_;
    Smoothed level_;
    Smoothed decay_;
    Smoothed tone_;
    Smoothed diffusion_;
};

} // namespace chompi
