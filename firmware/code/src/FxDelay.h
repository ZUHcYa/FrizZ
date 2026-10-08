/** @file FxDelay.h
 *  @brief TEMPO's tempo-synced delay as a send.
 */
#pragma once
#include "FxCommon.h"
#include "granularDelay.h"
#include "TempoClock.h"

namespace chompi
{

/** TEMPO's tempo-synced delay (granularDelay.h) as a send.
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

    void Init(float* buffer, size_t buffer_frames)
    {
        delay_.Init(buffer, buffer_frames);
        gate_.Init();
        level_.Reset(0.f);
        feedback_.Reset(.3f); // granularDelay's own
    }

    /** Once per block: the tempo, plus one call per clock pulse in this block, with the
     *  clock's position (TempoClock::Pulse). Every 8th note is an edge, where the delay rolls
     *  its random events, only while its key is on: the tail of one that's off is plain
     *  echoes. */
    void SetTempo(float bpm) { delay_.SetTempo(bpm); }
    void ClockPulse(uint32_t pos)
    {
        if (pos % kPulsesPerEdge == 0)
            delay_.setClockEdge(gate_.IsOn());
    }

    /** Feeds in_l / in_r into the delay (while on) and adds its return to *out_l / *out_r */
    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        const float gate = gate_.Process();
        const float level = level_.Process();
        // the feedback slews like the other knobs, so turning it doesn't zipper the repeats
        if (feedback_.value != feedback_.target)
        {
            feedback_.Process();
            if (fabsf(feedback_.value - feedback_.target) < 1e-5f)
                feedback_.Snap();
            delay_.setFeedback(feedback_.value);
        }

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
            delay_.setDivision(StepIndex(val, kNumDivisions));
            break;
        case FEEDBACK:
            feedback_.target = val;
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
    Smoothed feedback_;
};

} // namespace chompi
