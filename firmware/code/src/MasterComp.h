/** @file MasterComp.h
 *  @brief The master compressor: always on, after the punch-in FX and before the output gain
 *  (passthroughEngine.h), so VOLUME doesn't change how hard it works. Its knobs are edited like
 *  an FX's from its own key (FxControls.h, kCompParams in FxParams.h) and kept on the card
 *  (MasterSettings.h), not in the scenes.
 *
 *  One stereo-linked detector, the louder channel's peak, so the image doesn't shift, held for
 *  kHoldMs so a fast attack doesn't follow a bass note's waveform (crackle). Above the
 *  threshold, the gain falls by the ratio, with a soft knee; the auto makeup gives back half of
 *  what a signal at the 0dB reference loses. The reduction is smoothed in dB, so the release
 *  recovers evenly however deep it went. The reference is a signal of 1.0 at this point,
 *  about a hot line input at the default input gain.
 *
 *  Knobs, each 0..1:
 *   0 amount: the threshold, 0dB down to -30dB. 0 is off, an exact bypass
 *   1 ratio: 1.5:1 to 20:1, 4:1 in the middle
 *   2 speed: attack 1-30ms with release 40-600ms (time constants), fast to slow
 *   3 mix: dry to fully compressed, for parallel compression
 *
 *  The safety limiter after the output gain is the old master compressor at its lowest
 *  setting (limiter.h); this is in front of it.
 */
#pragma once
#include <math.h>
#include "daisysp.h"
#include "FxCommon.h"

namespace chompi
{

class MasterComp
{
public:
    static const size_t kAmount = 0, kRatio = 1, kSpeed = 2, kMix = 3;

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        reduction_ = 0.f;
        held_ = 0.f;
        hold_left_ = 0;
        hold_samples_ = static_cast<uint32_t>(kHoldMs * .001f * sample_rate);
        for (size_t p = 0; p < kNumFxParams; p++)
            knobs_[p].Reset(0.f);
        knobs_[kMix].Reset(1.f);
        Update();
    }

    /** From the UI, 0..1 */
    inline void SetParam(size_t param, float val) { knobs_[param].target = val; }

    /** One stereo sample, in place */
    void Process(float* l, float* r)
    {
        // the knobs slew; what follows from them is worked out only while one moves
        bool moved = false;
        for (size_t p = 0; p < kNumFxParams; p++)
            moved |= knobs_[p].Settle(kFxParamCoeff);
        if (moved)
            Update();

        // off: a bypass, once a reduction left from before has released, so turning it off
        // under a hot signal doesn't click
        const bool off = knobs_[kAmount].value == 0.f;
        if (off && reduction_ > -kOffDb)
        {
            reduction_ = 0.f;
            held_ = 0.f;
            hold_left_ = 0;
            return;
        }

        // the louder channel's peak, held for hold_samples_ before it may fall
        const float now = fmaxf(fabsf(*l), fabsf(*r));
        if (now >= held_)
        {
            held_ = now;
            hold_left_ = hold_samples_;
        }
        else if (hold_left_ > 0)
            hold_left_--;
        else
            held_ = now;
        const float peak = held_;

        // the reduction that peak calls for, <= 0, and then the attack (more) or release (less)
        // towards it
        float target = 0.f;
        if (!off && peak > knee_start_)
        {
            const float over = 6.0206f * daisysp::fastlog2f(peak) - thresh_db_;
            const float slope = 1.f / ratio_ - 1.f;
            target = over < kKneeDb * .5f
                         ? slope * (over + kKneeDb * .5f) * (over + kKneeDb * .5f) / (2.f * kKneeDb)
                         : slope * over;
        }
        reduction_ += (target < reduction_ ? attack_ : release_) * (target - reduction_);
        const float gain = daisysp::pow10f((reduction_ + makeup_db_) * .05f);

        const float mix = knobs_[kMix].value;
        *l += (*l * gain - *l) * mix;
        *r += (*r * gain - *r) * mix;
    }

    /** The gain reduction now, in dB (<= 0), for the key's LED */
    inline float GetReduction() const { return reduction_; }

private:
    static constexpr float kKneeDb = 6.f;
    static constexpr float kOffDb = .01f; // a reduction this small is gone
    static constexpr float kMaxThreshDb = 30.f;
    static constexpr float kHoldMs = 10.f; // a half-cycle of 50Hz

    /** The threshold, ratio, makeup and envelope times from the knobs */
    void Update()
    {
        static const float kRatioX[] = {0.f, .25f, .5f, .75f, 1.f};
        static const float kRatioY[] = {1.5f, 2.f, 4.f, 8.f, 20.f};
        thresh_db_ = -kMaxThreshDb * knobs_[kAmount].value;
        ratio_ = CurveMap(knobs_[kRatio].value, kRatioX, kRatioY, 5);
        makeup_db_ = -.5f * thresh_db_ * (1.f - 1.f / ratio_);
        // below where the knee starts nothing is reduced: no log needed
        knee_start_ = daisysp::pow10f((thresh_db_ - kKneeDb * .5f) * .05f);

        const float speed = knobs_[kSpeed].value;
        const float attack_ms = powf(30.f, speed);
        const float release_ms = 40.f * powf(15.f, speed);
        attack_ = 1.f - expf(-1000.f / (attack_ms * sample_rate_));
        release_ = 1.f - expf(-1000.f / (release_ms * sample_rate_));
    }

    float sample_rate_;
    Smoothed knobs_[kNumFxParams];
    float reduction_;  // dB, <= 0, smoothed
    float held_;       // the detector's held peak
    uint32_t hold_left_, hold_samples_;
    float thresh_db_, ratio_, makeup_db_, knee_start_;
    float attack_, release_;
};

} // namespace chompi
