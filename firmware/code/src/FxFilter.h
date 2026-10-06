/** @file FxFilter.h
 *  @brief The DJ filter with a tempo-synced LFO.
 */
#pragma once
#include "FxCommon.h"
#include "DJFilter.h"
#include "TempoClock.h"

namespace chompi
{

// The filter LFO's cycle in 12 PPQN pulses: 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars.
// Each divides kPulsesPerCycle, so the LFO stays on the beat grid whichever is picked.
static const uint32_t kLfoDivisionPulses[] = {3, 6, 12, 24, 48, 96, 192};
static_assert(kPulsesPerCycle % 192 == 0, "the longest LFO cycle must divide the clock's");

/** The DJ filter shared by TAPE, TEMPO and WAVE (DJFilter.h, WAVE's copy): lowpass below the
 *  centre of the cutoff knob, highpass above, flat in the middle. Plus a triangle LFO on the
 *  cutoff, like WAVE's filter LFO but locked to the tempo clock (TempoClock.h).
 *  Params: 0 cutoff, 1 resonance, 2 LFO depth, 3 LFO division (kNumLfoDivisions steps). */
class Filter : public FxBase
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

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        filter_.Init(sample_rate);
        filter_.SetSlew(1.f); // the cutoff is slewed here, so the LFO isn't smoothed away
        gate_.Init();
        tempo_ = 120;
        lfo_pulses_ = 0;
        lfo_frac_ = 0.f;
        lfo_div_pulses_ = 48;

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        cutoff_.Snap();
        depth_.Snap();
    }

    /** Once per block: the tempo, plus one call per clock pulse in this block, with the
     *  clock's position (TempoClock::Pulse) */
    void SetTempo(int bpm) { tempo_ = bpm; }
    /** reverse: the position counts down, so the LFO eases down towards the pulse before */
    void ClockPulse(uint32_t pos, bool reverse = false)
    {
        lfo_pulses_ = pos;
        lfo_frac_ = 0.f;
        lfo_reverse_ = reverse;
    }

    void Process(float* l, float* r)
    {
        const float gate = gate_.Process();
        const float cutoff = cutoff_.Process();
        const float depth = depth_.Process();

        // move smoothly between pulses at the tempo, but wait at the next pulse rather than
        // run past it, so a late MIDI clock tick doesn't make the phase jump back. In
        // reverse, the next pulse is the one below
        const float inc = static_cast<float>(tempo_) * 12.f / (60.f * sample_rate_);
        lfo_frac_ += lfo_reverse_ ? -inc : inc;
        if (lfo_frac_ > .999f)
            lfo_frac_ = .999f;
        else if (lfo_frac_ < -.999f)
            lfo_frac_ = -.999f;
        float pos = static_cast<float>(lfo_pulses_ % lfo_div_pulses_) + lfo_frac_;
        if (pos < 0.f)
            pos += static_cast<float>(lfo_div_pulses_);
        float phase = pos / static_cast<float>(lfo_div_pulses_) + .25f;
        if (phase >= 1.f)
            phase -= 1.f;
        // triangle: 0 on the beat, up to +1 (towards highpass) a quarter cycle later
        const float tri = 1.f - 4.f * fabsf(phase - .5f);

        // full depth sweeps +/-.5, the whole knob range from the centre
        filter_.SetControl(fclamp(cutoff + depth * .5f * tri, 0.f, 1.f));

        float fl, fr;
        filter_.Process(*l, *r, &fl, &fr);

        *l += gate * (fl - *l);
        *r += gate * (fr - *r);
    }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case CUTOFF:
            cutoff_.target = val;
            break;
        case RESONANCE:
            // WAVE's master resonance: the full range, limited just below 1
            filter_.SetRes(fclamp(val, 0.f, .99f));
            break;
        case LFO_DEPTH:
            depth_.target = val;
            break;
        case LFO_DIVISION:
            lfo_div_pulses_ = kLfoDivisionPulses[StepIndex(val, kNumLfoDivisions)];
            break;
        default:
            break;
        }
    }

private:
    float sample_rate_;
    DjFilter filter_;
    Smoothed cutoff_;
    Smoothed depth_;
    int tempo_;
    uint32_t lfo_pulses_;     // the clock's position
    float lfo_frac_;          // progress towards the next pulse, negative in reverse
    bool lfo_reverse_ = false;
    uint32_t lfo_div_pulses_; // pulses per LFO cycle
};

} // namespace chompi
