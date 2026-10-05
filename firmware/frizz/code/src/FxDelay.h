/** @file FxDelay.h
 *  @brief TEMPO's tempo-synced delay as a send.
 */
#pragma once
#include "FxCommon.h"
#include "granularDelay.h"
#include "TempoClock.h"

namespace chompi
{

/** TEMPO's tempo-synced delay (granularDelay.h) as a send. Its freeze isn't used, but it
 *  still needs the frozen buffer, which it writes every sample.
 *  Params: 0 division (9 steps), 1 feedback, 2 random (bipolar, 0.5 = off), 3 level. */
class DelaySend : public FxBase
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
        gate_.Init();
        level_.Reset(0.f);
    }

    /** Once per block: the tempo, plus one call per clock pulse in this block, with the
     *  clock's position (TempoClock::Pulse). Every 8th note is an edge, where the delay rolls
     *  its random events. */
    void SetTempo(int bpm) { delay_.SetTempo(bpm); }
    void ClockPulse(uint32_t pos)
    {
        delay_.setClockPulse();
        if (pos % kPulsesPerEdge == 0)
            delay_.setClockEdge();
    }

    /** Feeds in_l / in_r into the delay (while on) and adds its return to *out_l / *out_r */
    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        const float gate = gate_.Process();
        const float level = level_.Process();

        float dl = 0.f, dr = 0.f;
        delay_.write(in_l * gate, in_r * gate);
        delay_.read(&dl, &dr);

        *out_l += dl * level;
        *out_r += dr * level;
    }

    void SetParam(size_t param, float val) override
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
            level_.target = val;
            break;
        default:
            break;
        }
    }

private:
    granularDelay delay_;
    Smoothed level_;
};

} // namespace chompi
