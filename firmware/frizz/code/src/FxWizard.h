/** @file FxWizard.h
 *  @brief Punch-in inserts ported from Bastl Instruments' Kastle 2 FX Wizard
 *  (github.com/bastl-instruments/kastle2, code/src/apps/FxWizard/AppFxWizard.cpp,
 *  FxWizardParameterMaps.hpp): the freezer, slicer, flanger and shifter. Rewritten from Kastle's 44kHz fixed point to 48kHz float, with
 *  Kastle's mode knobs and trigger input replaced by a held key, and clocked by TempoClock
 *  pulses (12 PPQN, see TempoClock.h) like the filter's LFO.
 *
 *  The original code is under this license:
 *
 *  MIT License
 *
 *  Copyright (c) 2024 Marek Mach (Bastl Instruments)
 *  Copyright (c) 2024 Vaclav Mach (Bastl Instruments)
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */
#pragma once
#include "PunchFx.h"

namespace chompi
{

// One 16th note in TempoClock pulses
static const uint32_t kPulsesPer16th = 3;

// Freezer lengths, a bar divided by, short to long: 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1 bar
static const uint8_t kFreezerBarDivisions[] = {16, 12, 8, 6, 4, 3, 2, 1};

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

// Flanger maps: LFO rate in Hz by the rate knob, the right LFO's detune by the stereo knob
static const float kFlangerRateX[] = {0.f, .3f, .6f, .75f, 1.f};
static const float kFlangerRateHz[] = {.02f, .1f, 1.f, 5.f, 50.f};
static const float kFlangerDetuneX[] = {0.f, .1f, 1.f};
static const float kFlangerDetuneHz[] = {0.f, .5f, .2f};

// Shifter map: LFO rate in Hz by the shift knob, 1Hz at the centre, 100Hz at the bottom
// (down), 260Hz at the top (up)
static const float kShifterRateX[] = {0.f, .35f, .5f, .65f, 1.f};
static const float kShifterRateHz[] = {100.f, 1.2f, 1.f, 1.2f, 260.f};

// Kastle's slicer patterns, sparse to dense, 8 16th-note steps
static const uint8_t kSlicerPatterns[] = {
    0b10000000,
    0b10001000,
    0b00100010,
    0b10000100,
    0b10010010,
    0b10101010,
    0b10101100,
    0b11111111,
};

/** KEY_3: Kastle's freezer as a beat repeat. Pressing the key arms it; on the next 16th it
 *  starts recording, passing the live signal through for one loop length, then loops what
 *  it recorded until the key is released. It keeps recording past the loop for as long as
 *  the buffer lasts, so the length can be turned up while it repeats.
 *  Params: 0 length (kNumLengths steps), 1 feedback, 2 stereo, 3 pitch (0 = off).
 *  The freezer's buffers are separate (SDRAM, chompi_main.cpp), kFreezerFrames per channel. */
class Freezer
{
public:
    enum Param
    {
        LENGTH,
        FEEDBACK,
        STEREO,
        PITCH,
    };

    static const size_t kNumLengths = sizeof(kFreezerBarDivisions);

    void Init(float sample_rate, float* buf_l, float* buf_r, size_t frames)
    {
        sample_rate_ = sample_rate;
        buf_[0] = buf_l;
        buf_[1] = buf_r;
        frames_ = frames;
        state_ = State::IDLE;
        on_ = false;
        start_ = false;
        gate_ = gate_target_ = 0.f;
        tempo_ = 120;
        pulses_ = 0;
        written_ = 0;
        pos_[0] = pos_[1] = 0;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
    }

    /** Once per block: the tempo, plus one call per clock pulse (12 PPQN) in this block */
    void SetTempo(int bpm)
    {
        if (bpm != tempo_)
        {
            tempo_ = bpm;
            UpdateLengths();
        }
    }
    void ClockPulse()
    {
        pulses_ = (pulses_ + 1) % kPulsesPer16th;
        if (pulses_ == 0 && state_ == State::ARMED)
            start_ = true;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);

        // back to idle once the release has faded out
        if (!on_ && state_ != State::IDLE && gate_ < .001f)
            state_ = State::IDLE;

        if (start_)
        {
            start_ = false;
            if (state_ == State::ARMED)
            {
                state_ = State::RUNNING;
                written_ = 0;
                pos_[0] = pos_[1] = 0;
            }
        }

        // idle or waiting for the 16th: the live signal passes
        if (state_ != State::RUNNING)
            return;

        float* const io[2] = {l, r};

        // record forward until the buffer is full
        const bool recording = written_ < frames_;
        if (recording)
        {
            buf_[0][written_] = *l;
            buf_[1][written_] = *r;
            written_++;
        }

        for (size_t c = 0; c < 2; c++)
        {
            const size_t target = len_[c];

            // the first pass: still recording the loop, the live signal passes
            if (written_ <= target && recording)
                continue;

            // can't loop more than has been recorded
            const size_t len = target < written_ ? target : written_;
            if (pos_[c] >= len)
                pos_[c] = 0;
            const size_t pos = pos_[c];

            // Kastle has no seam crossfade; this blends the loop start with what was recorded
            // just after its end, so the first repeat follows the live signal seamlessly
            float* const b = buf_[c];
            float wet = b[pos];
            const size_t xfade = len / 4 < kXfadeFrames ? len / 4 : kXfadeFrames;
            if (pos < xfade && len + pos < written_)
            {
                const float t = static_cast<float>(pos) / static_cast<float>(xfade);
                wet = wet * t + b[len + pos] * (1.f - t);
            }

            // feedback: the input overdubbed into the loop, which fades a little
            if (fb_in_ > .0001f)
                b[pos] = 2.f * SoftClip(.5f * (b[pos] * fb_keep_ + *io[c] * fb_in_));

            pos_[c] = pos + 1;
            *io[c] += gate_ * (wet - *io[c]);
        }
    }

    inline void SetOn(bool on)
    {
        // on_ first, so the audio callback can't drop the new capture back to idle
        const bool was_on = on_;
        on_ = on;
        if (on && !was_on)
            state_ = State::ARMED; // a new capture, also during a release fade-out
        gate_target_ = on ? 1.f : 0.f;
    }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case LENGTH:
            length_idx_ = static_cast<size_t>(val * (kNumLengths - 1) + .5f);
            break;
        case FEEDBACK:
            // Kastle's maps: input in 0 / .3 / .8 and the loop kept 1 / 1 / .9 at 0 / .75 / 1
            if (val < .75f)
            {
                fb_in_ = val / .75f * .3f;
                fb_keep_ = 1.f;
            }
            else
            {
                const float t = (val - .75f) / .25f;
                fb_in_ = .3f + t * .5f;
                fb_keep_ = 1.f - t * .1f;
            }
            break;
        case STEREO:
            // Kastle: the left loop up to 2000 samples at 44kHz longer
            stereo_ = static_cast<size_t>(val * kMaxStereoFrames);
            break;
        case PITCH:
            pitch_ = val;
            break;
        default:
            break;
        }
        UpdateLengths();
    }

private:
    enum class State
    {
        IDLE,
        ARMED,
        RUNNING,
    };

    // Kastle's pitched loops, 880 to 150 samples at 44kHz: 50Hz up to 293Hz
    static constexpr float kPitchLongest = 960.f;
    static constexpr float kPitchShortest = 164.f;
    static const size_t kMaxStereoFrames = 2180; // 45ms
    static const size_t kXfadeFrames = 240;      // 5ms, like the looper's

    void UpdateLengths()
    {
        size_t len;
        if (pitch_ > 0.f)
            len = static_cast<size_t>(kPitchLongest * powf(kPitchShortest / kPitchLongest, pitch_));
        else
            len = static_cast<size_t>(240.f * sample_rate_ / static_cast<float>(tempo_))
                  / kFreezerBarDivisions[length_idx_];

        const size_t max = frames_ - 1;
        len_[0] = len + stereo_ < max ? len + stereo_ : max;
        len_[1] = len < max ? len : max;
    }

    float sample_rate_;
    float* buf_[2];
    size_t frames_;
    volatile State state_;
    volatile bool on_;
    volatile bool start_;
    float gate_, gate_target_;
    int tempo_;
    uint32_t pulses_; // mod kPulsesPer16th
    size_t written_;  // frames recorded since the capture started
    size_t pos_[2];   // loop read position, per channel
    size_t len_[2];   // target loop length, per channel
    size_t length_idx_ = 0;
    size_t stereo_ = 0;
    float pitch_ = 0.f;
    float fb_in_ = 0.f, fb_keep_ = 1.f;
};

/** KEY_4: Kastle's slicer. A gate on 16th-note steps: each step of the pattern that's on
 *  retriggers an envelope (10ms attack, then a decay) that the signal is multiplied by.
 *  Steps are counted from the clock pulses, so an 8-step pattern spans half a bar.
 *  Pressing the key also triggers it, so the signal doesn't drop out until the next step.
 *  Params: 0 pattern (kNumPatterns steps), 1 decay, 2 chance, 3 stereo (kNumPatterns steps).
 *  Chance flips every step of the pattern, on both channels, at random. Stereo plays the
 *  pattern that many patterns up on the left and down on the right. */
class Slicer
{
public:
    enum Param
    {
        PATTERN,
        DECAY,
        CHANCE,
        STEREO,
    };

    static const size_t kNumPatterns = sizeof(kSlicerPatterns);

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        attack_inc_ = 1.f / (.01f * sample_rate);
        gate_ = gate_target_ = 0.f;
        env_[0] = env_[1] = 0.f;
        attacking_[0] = attacking_[1] = false;
        pulses_ = 0;
        step_ = false;
        trigger_ = false;
        on_ = false;
        rng_ = 0x2545F491u;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
    }

    /** One call per clock pulse (12 PPQN) */
    void ClockPulse()
    {
        pulses_ = (pulses_ + 1) % (kPulsesPer16th * kNumSteps);
        if (pulses_ % kPulsesPer16th == 0)
            step_ = true;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);

        if (trigger_)
        {
            trigger_ = false;
            attacking_[0] = attacking_[1] = true;
        }

        if (step_)
        {
            step_ = false;
            const uint32_t step = pulses_ / kPulsesPer16th;
            rng_ ^= rng_ << 13;
            rng_ ^= rng_ >> 17;
            rng_ ^= rng_ << 5;
            const bool flip = static_cast<float>(rng_ >> 8) * (1.f / 16777216.f) < chance_;

            const size_t pattern[2] = {
                (pattern_ + stereo_) % kNumPatterns,
                (pattern_ + kNumPatterns - stereo_) % kNumPatterns,
            };
            for (size_t c = 0; c < 2; c++)
            {
                // bit 7 is the first step, so 0b10001000 is quarter notes (Kastle reads the
                // bits from the other end)
                const bool hit = (kSlicerPatterns[pattern[c]] >> (kNumSteps - 1 - step)) & 1;
                if (hit != flip)
                    attacking_[c] = true;
            }
        }

        float* const io[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            if (attacking_[c])
            {
                env_[c] += attack_inc_;
                if (env_[c] >= 1.f)
                {
                    env_[c] = 1.f;
                    attacking_[c] = false;
                }
            }
            else
                env_[c] *= decay_coeff_;

            *io[c] += gate_ * (*io[c] * env_[c] - *io[c]);
        }
    }

    inline void SetOn(bool on)
    {
        if (on && !on_)
            trigger_ = true;
        on_ = on;
        gate_target_ = on ? 1.f : 0.f;
    }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case PATTERN:
            pattern_ = static_cast<size_t>(val * (kNumPatterns - 1) + .5f);
            break;
        case DECAY:
        {
            // Kastle: 1s down to 10ms; here short to long, the time to fall by 60dB
            const float time = .01f * powf(100.f, val);
            decay_coeff_ = expf(-6.9078f / (time * sample_rate_));
            break;
        }
        case CHANCE:
            chance_ = val * .9f; // Kastle's maximum
            break;
        case STEREO:
            stereo_ = static_cast<size_t>(val * (kNumPatterns - 1) + .5f);
            break;
        default:
            break;
        }
    }

private:
    static const uint32_t kNumSteps = 8;

    float sample_rate_;
    float attack_inc_;
    float gate_, gate_target_;
    float env_[2];
    bool attacking_[2];
    uint32_t pulses_; // mod one pattern, kNumSteps 16ths
    volatile bool step_;
    volatile bool trigger_;
    bool on_;
    uint32_t rng_;
    size_t pattern_ = 0;
    size_t stereo_ = 0;
    float chance_ = 0.f;
    float decay_coeff_ = 0.f;
};

/** KEY_5: Kastle's flanger. A delay of about 12ms swept either way by a triangle LFO, mixed
 *  with the input. Like Kastle's Amount, one knob sets both the sweep depth and the mix, so
 *  the top of it is pure vibrato. Pressing the key restarts the sweep (Kastle's trigger).
 *  Feedback recirculates the swept delay, a classic flanger's resonance; Kastle's Feedback
 *  is instead a short comb around every mode, at most 8% for the flanger.
 *  Params: 0 rate, 1 amount, 2 feedback, 3 stereo. */
class Flanger
{
public:
    enum Param
    {
        RATE,
        AMOUNT,
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
        gate_ = gate_target_ = 0.f;
        reset_ = false;
        on_ = false;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        depth_ = depth_target_;
        mix_ = mix_target_;
        feedback_ = feedback_target_;
        stereo_mix_ = stereo_mix_target_;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(depth_, depth_target_, .001f);
        fonepole(mix_, mix_target_, .001f);
        fonepole(feedback_, feedback_target_, .001f);
        fonepole(stereo_mix_, stereo_mix_target_, .001f);

        if (reset_)
        {
            reset_ = false;
            phase_[0] = phase_[1] = 0.f;
        }

        // triangle LFOs, -1..1, starting at 0 going up
        float lfo[2];
        for (size_t c = 0; c < 2; c++)
        {
            phase_[c] += inc_[c];
            if (phase_[c] >= 1.f)
                phase_[c] -= 1.f;
            lfo[c] = phase_[c] < .25f   ? 4.f * phase_[c]
                     : phase_[c] < .75f ? 2.f - 4.f * phase_[c]
                                        : 4.f * phase_[c] - 4.f;
        }
        // Kastle: the right LFO is the left one until stereo is turned up
        if (stereo_mix_target_ >= 1.f)
            phase_[1] = phase_[0];
        lfo[1] = stereo_mix_ * lfo[0] + (1.f - stereo_mix_) * lfo[1];

        float* const io[2] = {l, r};
        for (size_t c = 0; c < 2; c++)
        {
            const float delay = fclamp(kCentreFrames * (1.f + lfo[c] * depth_), 1.f, kBufSize - 2.f);
            const float wet = ReadFrac(buf_[c], kBufMask, write_pos_ - 1, delay);
            buf_[c][write_pos_] = SoftClip(*io[c] + wet * feedback_);

            const float out = *io[c] + mix_ * (wet - *io[c]);
            *io[c] += gate_ * (out - *io[c]);
        }
        write_pos_ = (write_pos_ + 1) & kBufMask;
    }

    inline void SetOn(bool on)
    {
        if (on && !on_)
            reset_ = true;
        on_ = on;
        gate_target_ = on ? 1.f : 0.f;
    }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case RATE:
            rate_ = CurveMap(val, kFlangerRateX, kFlangerRateHz, 5);
            break;
        case AMOUNT:
        {
            // Kastle: depth 0 / .5 / 1 at 0 / .2 / 1, and the mix 0 to 1
            static const float xs[] = {0.f, .2f, 1.f};
            static const float ys[] = {0.f, .5f, 1.f};
            depth_target_ = CurveMap(val, xs, ys, 3);
            mix_target_ = val;
            break;
        }
        case FEEDBACK:
            feedback_target_ = val * .85f;
            break;
        case STEREO:
            stereo_ = val;
            stereo_mix_target_ = fclamp(1.f - val / .2f, 0.f, 1.f);
            break;
        default:
            break;
        }
        inc_[0] = rate_ / sample_rate_;
        inc_[1] = (rate_ + CurveMap(stereo_, kFlangerDetuneX, kFlangerDetuneHz, 3)) / sample_rate_;
    }

private:
    static const size_t kBufSize = 2048; // 2 x the deepest sweep, 1114 frames
    static const size_t kBufMask = kBufSize - 1;
    static constexpr float kCentreFrames = 557.f; // Kastle's 511 at 44kHz, 11.6ms

    float sample_rate_;
    float buf_[2][kBufSize];
    size_t write_pos_;
    float phase_[2];
    float inc_[2] = {0.f, 0.f};
    float rate_ = 0.f, stereo_ = 0.f;
    float gate_, gate_target_;
    float depth_, depth_target_;
    float mix_, mix_target_;
    float feedback_, feedback_target_;
    float stereo_mix_, stereo_mix_target_;
    volatile bool reset_;
    bool on_;
};

/** KEY_6: Kastle's shifter, a one-tap delay-line pitch shifter: an LFO sweeps the delay
 *  across 11.6ms as a ramp, and the tap fades out and in around each wrap (Kastle's 64
 *  samples at 44kHz). Shift's distance from centre sets the LFO rate, its side the direction:
 *  right of centre up, left down, the centre itself dry. Near the centre that's a slight
 *  detune, further out a shift of several semitones, and towards the ends the fades turn it
 *  into a buzzing ring-mod-like tone, at up to 100Hz down and 260Hz up, as on Kastle.
 *  Swoop is Kastle's trigger envelope on the rate (0.1s up, 1s down), fired by the key
 *  press, up to 8x down and 20x up; Kastle tied its depth to the shift knob. Feedback recirculates the
 *  shifted output into the delay, so the shift spirals (Kastle: its comb around every mode).
 *  Params: 0 shift (bipolar, 0.5 = off), 1 swoop, 2 feedback, 3 stereo. */
class Shifter
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
        gate_ = gate_target_ = 0.f;
        env_ = 0.f;
        env_attacking_ = false;
        trigger_ = false;
        on_ = false;
        env_attack_inc_ = 1.f / (.1f * sample_rate);
        env_decay_coeff_ = expf(-6.9078f / sample_rate); // 1s to -60dB

        SetParam(SHIFT, .5f);
        for (size_t i = 1; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        dry_ = dry_target_;
        feedback_ = feedback_target_;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(dry_, dry_target_, .002f);
        fonepole(feedback_, feedback_target_, .001f);

        if (trigger_)
        {
            trigger_ = false;
            env_attacking_ = true;
        }
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

            buf_[c][write_pos_] = SoftClip(*io[c] + wet * feedback_);

            const float out = wet + dry_ * (*io[c] - wet);
            *io[c] += gate_ * (out - *io[c]);
        }
        write_pos_ = (write_pos_ + 1) & kBufMask;
    }

    inline void SetOn(bool on)
    {
        if (on && !on_)
            trigger_ = true;
        on_ = on;
        gate_target_ = on ? 1.f : 0.f;
    }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case SHIFT:
        {
            shift_ = val;
            up_ = val > .5f;
            // Kastle: dry in .48-.52, fading to fully wet by .47 / .53
            const float d = fabsf(val - .5f);
            dry_target_ = fclamp((.03f - d) / .01f, 0.f, 1.f);
            break;
        }
        case SWOOP:
            swoop_ = val;
            break;
        case FEEDBACK:
            feedback_target_ = val * .45f; // Kastle's comb: up to .35, plus its input boost
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
    float gate_, gate_target_;
    float dry_, dry_target_;
    float feedback_, feedback_target_;
    float env_, env_attack_inc_, env_decay_coeff_;
    bool env_attacking_;
    volatile bool trigger_;
    bool on_;
};

} // namespace chompi
