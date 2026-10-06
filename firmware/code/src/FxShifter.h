/** @file FxShifter.h
 *  @brief The shifter, a two-tap delay-line pitch shifter tuned in semitones.
 *
 *  The swoop, the feedback and the stereo offset are after Bastl Instruments' Kastle 2 FX
 *  Wizard shifter (github.com/bastl-instruments/kastle2, code/src/apps/FxWizard/).
 *  Copyright (c) 2024 Marek Mach, Vaclav Mach (Bastl Instruments), MIT License: see
 *  LICENSE-kastle2.
 */
#pragma once
#include "FxCommon.h"

namespace chompi
{

/** A pitch shifter in semitones. Two taps read the input at 2^(semitones/12) its speed,
 *  each living for a 30ms stretch of delay before it wraps and starts over, half a life
 *  apart and crossfaded with triangles that always sum to one.
 *
 *  Where a tap starts over is searched for: within +-7.5ms of its nominal start, the place
 *  whose next samples best match what the other tap is about to play (every 6th offset,
 *  then every offset around the best, comparing 16 samples spread over 128). Without that, the two
 *  taps cross in an arbitrary phase relation, which pulls the pitch off by up to most of a
 *  semitone and makes the level wobble. (Kastle's shifter has one tap that fades out and in
 *  at every wrap, which is what made it buzz.)
 *
 *  Swoop is Kastle's trigger envelope (0.1s up, 1s down), fired by the key press, pushing
 *  the shift up to 2 octaves further in its direction and back. Feedback recirculates the
 *  shifted output, so each pass shifts again and the shift spirals.
 *  Params: 0 shift (kNumShifts steps, -12 to +12 semitones, the centre dry), 1 swoop,
 *  2 feedback, 3 stereo (the right channel up to a semitone higher). */
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

    static const size_t kNumShifts = 25; // -12..+12 semitones

    void Init(float sample_rate)
    {
        for (size_t c = 0; c < 2; c++)
        {
            for (size_t i = 0; i < kBufSize; i++)
                buf_[c][i] = 0.f;
            window_[c] = 0.f;
            delay_[c][0] = kGuard + static_cast<float>(kSearch);
            delay_[c][1] = kGuard + static_cast<float>(kSearch) + .5f * kWindowFrames;
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

        // the speed per channel; recomputed every sample while swooping, otherwise when a
        // knob changed it (SetParam), so only the audio callback writes it
        const float swoop = env_ * swoop_;
        if (swoop > .0001f || swooping_)
        {
            const float dir = semitones_ > 0 ? 1.f : (semitones_ < 0 ? -1.f : 0.f);
            ratios_changed_ = false;
            UpdateRatios(semitones_ + dir * swoop * kSwoopSemitones);
            swooping_ = swoop > .0001f;
        }
        else if (ratios_changed_)
        {
            ratios_changed_ = false;
            UpdateRatios(static_cast<float>(semitones_));
        }

        float* const io[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            const float ratio = ratio_[c];
            const bool up = ratio > 1.f;
            float* const d = delay_[c];

            // both taps' delays move by 1 - ratio a sample; the window moves through their
            // lives at the same pace, so each tap covers kWindowFrames of delay per life
            d[0] = fclamp(d[0] + 1.f - ratio, 1.f, kMaxDelay);
            d[1] = fclamp(d[1] + 1.f - ratio, 1.f, kMaxDelay);
            const float prev = window_[c];
            float w = prev + fabsf(1.f - ratio) / kWindowFrames;
            if (w >= 1.f)
            {
                w -= 1.f;
                d[0] = Splice(c, d[1], ratio, up); // tap 0 is silent here
            }
            else if (prev < .5f && w >= .5f)
                d[1] = Splice(c, d[0], ratio, up); // tap 1 is silent here
            window_[c] = w;

            // triangles: tap 0 peaks mid-window and is silent at the wrap, tap 1 the reverse
            const float w0 = 1.f - fabsf(2.f * w - 1.f);
            const float a = ReadFrac(buf_[c], kBufMask, write_pos_ - 1, d[0]);
            const float b = ReadFrac(buf_[c], kBufMask, write_pos_ - 1, d[1]);
            const float wet = a * w0 + b * (1.f - w0);

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
            semitones_ = static_cast<int>(StepIndex(val, kNumShifts)) - (kNumShifts - 1) / 2;
            dry_.target = semitones_ == 0 ? 1.f : 0.f;
            break;
        case SWOOP:
            swoop_ = val;
            break;
        case FEEDBACK:
            feedback_.target = val * .6f;
            break;
        case STEREO:
            stereo_semitones_ = val;
            break;
        default:
            break;
        }
        ratios_changed_ = true;
    }

private:
    static const size_t kBufSize = 4096;
    static const size_t kBufMask = kBufSize - 1;
    static constexpr float kWindowFrames = 1440.f; // a tap's life, 30ms of delay
    static const int kSearch = 360;                // +-7.5ms: aligns notes down to 67Hz
    static const int kCoarseStep = 6;              // the search's first pass, in samples,
    static const int kFineRange = 5;               // then every offset this close to the best
    static const size_t kCorrPoints = 16;          // compared over 128 samples,
    static const size_t kCorrStep = 8;             // every 8th
    // the closest a tap starts to the write head, so the comparison's reads exist yet: the
    // candidates read up to 120 samples on, times the speed, which is below 1 going down
    static constexpr float kGuard = 130.f;
    static constexpr float kMaxDelay = kWindowFrames + kGuard + 2.f * kSearch + kFineRange + 2.f;
    static constexpr float kSwoopSemitones = 24.f; // how far the swoop pushes at the top

    /** Where a tap that's starting over should start: its nominal start (the far end of
     *  the window going up, the near end going down) moved by up to kSearch to where its next
     *  samples best match the other tap's, by normalised correlation */
    float Splice(size_t c, float other, float ratio, bool up) const
    {
        const float* const b = buf_[c];
        const size_t last = write_pos_ - 1;
        const float nominal = (up ? kWindowFrames : 0.f) + kGuard + kSearch;

        // k samples on, both taps will have moved k * ratio through the input: the other
        // tap's samples, and where a candidate at lag 0 reads, once for every lag
        float ref[kCorrPoints];
        size_t start[kCorrPoints];
        for (size_t k = 0; k < kCorrPoints; k++)
        {
            const float ahead = static_cast<float>(k * kCorrStep) * ratio;
            ref[k] = b[(last - static_cast<size_t>(fmaxf(other - ahead, 1.f))) & kBufMask];
            start[k] = last - static_cast<size_t>(nominal - ahead);
        }

        // a lag adds to the delay, so the candidate reads lag samples further back
        auto score = [&](int lag) {
            float dot = 0.f, energy = 0.f;
            for (size_t k = 0; k < kCorrPoints; k++)
            {
                const float x = b[(start[k] - static_cast<size_t>(lag)) & kBufMask];
                dot += x * ref[k];
                energy += x * x;
            }
            return dot / sqrtf(energy + 1e-9f);
        };

        int best = 0;
        float best_score = -1e30f;
        for (int lag = -kSearch; lag <= kSearch; lag += kCoarseStep)
        {
            const float v = score(lag);
            if (v > best_score)
            {
                best_score = v;
                best = lag;
            }
        }
        const int coarse = best;
        for (int lag = coarse - kFineRange; lag <= coarse + kFineRange; lag++)
        {
            const float v = score(lag);
            if (v > best_score)
            {
                best_score = v;
                best = lag;
            }
        }
        return nominal + static_cast<float>(best);
    }

    /** Each channel's speed: 2^(semitones/12), the right channel stereo semitones higher */
    void UpdateRatios(float semitones)
    {
        ratio_[0] = powf(2.f, semitones / 12.f);
        ratio_[1] = powf(2.f, (semitones + stereo_semitones_) / 12.f);
    }

    float buf_[2][kBufSize];
    size_t write_pos_;
    float window_[2];   // through the taps' lives, 0..1; tap 0 starts over at 0, tap 1 at .5
    float delay_[2][2]; // per channel, per tap, frames behind the last write
    float ratio_[2] = {1.f, 1.f};
    volatile bool ratios_changed_ = false; // set by SetParam, picked up by Process
    int semitones_ = 0;
    float swoop_ = 0.f;
    bool swooping_ = false;
    float stereo_semitones_ = 0.f;
    Smoothed dry_;
    Smoothed feedback_;
    float env_, env_attack_inc_, env_decay_coeff_;
    bool env_attacking_;
};

} // namespace chompi
