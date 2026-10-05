/** @file PunchFx.h
 *  @brief Punch-in effects for the white keys. Each effect is on while its key is held or
 *  latched (see NormalPage.h) and has kNumFxParams parameters, all 0..1, set by the four
 *  free knobs.
 *
 *  An FX may use fewer than kNumFxParams; kFxNumParams says how many, the rest of the knobs
 *  do nothing while it's selected.
 *
 *  Two kinds:
 *   - insert (Filter, Crusher, and Freezer, Slicer, Flanger and Shifter in FxWizard.h):
 *     replaces the signal while on; only the wet amount is gated.
 *   - send (DelaySend, ReverbSend): the key gates what goes into the effect, and the effect's
 *     return is added to the signal, so tails ring out after the key is released.
 *  Either way the gate slews over ~5ms so punching in and out doesn't click, and effects
 *  process every sample even while off, so engaging one never starts from stale state.
 *
 *  The engine runs them on the summed output, after the dry/wet mix and before the output
 *  gain and master compressor: freezer, slicer, flanger, shifter, filter, crusher, then both
 *  sends in parallel from the crusher's output (see passthroughEngine.h). The resonator
 *  (FxWizard.h) is a feedback loop around the inserts after the freezer.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "granularDelay.h"
#include "reverb.h"

using namespace daisysp;

namespace chompi
{

static const size_t kNumFxParams = 4;

enum FxId
{
    FX_FILTER,
    FX_CRUSHER,
    FX_DELAY,
    FX_REVERB,
    FX_FREEZER, // FxWizard.h
    FX_SLICER,  // FxWizard.h
    FX_FLANGER, // FxWizard.h
    FX_SHIFTER, // FxWizard.h
    FX_RESONATOR, // FxWizard.h
    kNumFx,
};

// Knobs used per FxId, the first kFxNumParams[fx] of the four
static const size_t kFxNumParams[] = {4, 4, 4, 4, 4, 4, 4, 4, 4};
static_assert(sizeof(kFxNumParams) / sizeof(kFxNumParams[0]) == kNumFx, "one per FxId");

// ~5ms at 48kHz
static const float kFxGateCoeff = .004f;

// The filter LFO's cycle in 12 PPQN pulses: 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars
static const uint32_t kLfoDivisionPulses[] = {3, 6, 12, 24, 48, 96, 192};

/** KEY_1: the DJ filter shared by TAPE, TEMPO and WAVE (DJFilter.h, WAVE's copy): lowpass
 *  below the centre of the cutoff knob, highpass above, flat in the middle. Plus a triangle
 *  LFO on the cutoff, like WAVE's filter LFO but locked to the delay's tempo clock
 *  (TempoClock.h).
 *  Params: 0 cutoff, 1 resonance, 2 LFO depth, 3 LFO division (kNumLfoDivisions steps). */
class Filter
{
public:
    enum Param
    {
        CUTOFF,
        RESONANCE,
        LFO_DEPTH,
        LFO_DIVISION,
    };

    static const size_t kNumLfoDivisions = 7;
    static const uint32_t kLfoCyclePulses = 192; // 4 bars, a multiple of every division

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        filter_.Init(sample_rate);
        filter_.SetSlew(1.f); // the cutoff is slewed here, so the LFO isn't smoothed away
        gate_ = gate_target_ = 0.f;
        tempo_ = 120;
        lfo_pulses_ = 0;
        lfo_frac_ = 0.f;
        lfo_div_pulses_ = 48;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        cutoff_ = cutoff_target_;
        depth_ = depth_target_;
    }

    /** Once per block: the tempo, plus one call per clock pulse (12 PPQN) in this block */
    void SetTempo(int bpm) { tempo_ = bpm; }
    void ClockPulse()
    {
        // counting over 4 bars keeps the LFO on the beat grid whichever division is picked
        lfo_pulses_ = (lfo_pulses_ + 1) % kLfoCyclePulses;
        lfo_frac_ = 0.f;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(cutoff_, cutoff_target_, .001f);
        fonepole(depth_, depth_target_, .001f);

        // move smoothly between pulses at the tempo, but wait at the next pulse rather than
        // run past it, so a late MIDI clock tick doesn't make the phase jump back
        lfo_frac_ += static_cast<float>(tempo_) * 12.f / (60.f * sample_rate_);
        if (lfo_frac_ > .999f)
            lfo_frac_ = .999f;
        const float pos = static_cast<float>(lfo_pulses_ % lfo_div_pulses_) + lfo_frac_;
        float phase = pos / static_cast<float>(lfo_div_pulses_) + .25f;
        if (phase >= 1.f)
            phase -= 1.f;
        // triangle: 0 on the beat, up to +1 (towards highpass) a quarter cycle later
        const float tri = 1.f - 4.f * fabsf(phase - .5f);

        // full depth sweeps +/-.5, the whole knob range from the centre
        filter_.SetControl(fclamp(cutoff_ + depth_ * .5f * tri, 0.f, 1.f));

        float fl, fr;
        filter_.Process(*l, *r, &fl, &fr);

        *l += gate_ * (fl - *l);
        *r += gate_ * (fr - *r);
    }

    inline void SetOn(bool on) { gate_target_ = on ? 1.f : 0.f; }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case CUTOFF:
            cutoff_target_ = val;
            break;
        case RESONANCE:
            // WAVE's master resonance: the full range, limited just below 1
            filter_.SetRes(fclamp(val, 0.f, .99f));
            break;
        case LFO_DEPTH:
            depth_target_ = val;
            break;
        case LFO_DIVISION:
            lfo_div_pulses_ = kLfoDivisionPulses[static_cast<size_t>(val * (kNumLfoDivisions - 1) + .5f)];
            break;
        default:
            break;
        }
    }

private:
    float sample_rate_;
    DjFilter filter_;
    float gate_, gate_target_;
    float cutoff_, cutoff_target_;
    float depth_, depth_target_;
    int tempo_;
    uint32_t lfo_pulses_;     // pulses counted, mod kLfoCyclePulses
    float lfo_frac_;          // progress towards the next pulse
    uint32_t lfo_div_pulses_; // pulses per LFO cycle
};

/** KEY_2: TEMPO's sample-rate reducer, plus bit-depth reduction, a tone control, and two
 *  extras from the Kastle 2 FX Wizard's crusher (MIT, see FxWizard.h): XOR, which flips
 *  fixed bits of every 16-bit sample for a digital buzz, and a dive on every key press,
 *  the rate dropping up to 10x over 0.1s and recovering over 0.4s. Fully wet while on.
 *  Params: 0 rate, 1 bits, 2 tone, 3 XOR (0 = off). */
class Crusher
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
        dive_ = 0.f;
        dive_attacking_ = false;
        dive_trigger_ = false;
        on_ = false;
        dive_attack_inc_ = 1.f / (.1f * sample_rate);
        dive_decay_coeff_ = expf(-6.9078f / (.4f * sample_rate)); // to -60dB in 0.4s

        lp_l_ = lp_r_ = 0.f;
        gate_ = gate_target_ = 0.f;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        rate_ = rate_target_;
        tone_coeff_ = tone_coeff_target_;
    }

    void Process(float* l, float* r)
    {
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(rate_, rate_target_, .001f);
        fonepole(tone_coeff_, tone_coeff_target_, .001f);

        if (dive_trigger_)
        {
            dive_trigger_ = false;
            dive_attacking_ = true;
        }
        if (dive_attacking_)
        {
            dive_ += dive_attack_inc_;
            if (dive_ >= 1.f)
            {
                dive_ = 1.f;
                dive_attacking_ = false;
            }
        }
        else
            dive_ *= dive_decay_coeff_;

        // Kastle: the rate divided by the dive envelope times 10, at least 1
        const float rate = rate_ / fmaxf(1.f, dive_ * 10.f);
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
        const float inv_step = 1.f / step;
        wl = floorf(wl * inv_step + .5f) * step;
        wr = floorf(wr * inv_step + .5f) * step;

        // one-pole lowpass to tame the aliasing
        lp_l_ += tone_coeff_ * (wl - lp_l_);
        lp_r_ += tone_coeff_ * (wr - lp_r_);

        *l += gate_ * (lp_l_ - *l);
        *r += gate_ * (lp_r_ - *r);
    }

    inline void SetOn(bool on)
    {
        if (on && !on_)
            dive_trigger_ = true;
        on_ = on;
        gate_target_ = on ? 1.f : 0.f;
    }

    void SetParam(size_t param, float val)
    {
        switch (param)
        {
        case XOR:
        {
            // Kastle: 0 / 1000 / 2000 / 4000 over the top 30% of its Amount knob, here over
            // the whole knob
            static const float xs[] = {0.f, .233f, .667f, 1.f};
            static const float ys[] = {0.f, 1000.f, 2000.f, 4000.f};
            float k = ys[3];
            for (size_t i = 1; i < 4; i++)
            {
                if (val <= xs[i])
                {
                    k = ys[i - 1] + (ys[i] - ys[i - 1]) * (val - xs[i - 1]) / (xs[i] - xs[i - 1]);
                    break;
                }
            }
            xor_ = static_cast<int16_t>(k);
            break;
        }

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
    float tone_coeff_, tone_coeff_target_;
    float step_; // quantizer step, 2^(1 - bits)
    int16_t xor_ = 0;
    daisysp::DcBlock xor_dc_l_, xor_dc_r_;
    float dive_, dive_attack_inc_, dive_decay_coeff_;
    bool dive_attacking_;
    volatile bool dive_trigger_;
    bool on_;

    /** Kastle's XOR on the sample as 16-bit */
    inline float Xor(float x) const
    {
        const int16_t i = static_cast<int16_t>(fclamp(x, -1.f, 1.f) * 32767.f);
        return static_cast<float>(static_cast<int16_t>(i ^ xor_)) * (1.f / 32767.f);
    }
};

/** TEMPO's tempo-synced delay (granularDelay.h) as a send. Its freeze isn't used, but it
 *  still needs the frozen buffer, which it writes every sample.
 *  Params: 0 division (9 steps), 1 feedback, 2 random (bipolar, 0.5 = off), 3 level. */
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
        fonepole(gate_, gate_target_, kFxGateCoeff);
        fonepole(level_, level_target_, .001f);

        float dl = 0.f, dr = 0.f;
        delay_.write(in_l * gate_, in_r * gate_);
        delay_.read(&dl, &dr);

        *out_l += dl * level_;
        *out_r += dr * level_;
    }

    inline void SetOn(bool on) { gate_target_ = on ? 1.f : 0.f; }

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
