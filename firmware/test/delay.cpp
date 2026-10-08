// Delay check: the delay's voices (granularDelay.h) start where its events expect them.
// Built and run by unit.sh delay.
//  - right after Init, a pitch-up event reads from half a bar back, as it does after a
//    division change: the voices start on the delay's own division
#include <cmath>
#include "granularDelay.h"
#include "check.h"

static const size_t kFrames = 480000; // FRIZZ's 10s buffer
static float buffer[kFrames * 2];

/** How far back a voice's read head starts, in samples, from the write head */
static float Behind(const delayVoice& v)
{
    float behind = static_cast<float>(v.write_head_) - v.read_head_;
    if (behind < 0.f)
        behind += static_cast<float>(kFrames);
    return behind;
}

static void TestPitchUp(bool turn_division)
{
    static granularDelay d;
    d.Init(buffer, kFrames);
    if (turn_division)
    {
        // over to 1/2 and back to 1/4, as a turn of the knob does
        d.setDivision(5);
        for (int i = 0; i < 2000; i++)
        {
            float l = 0.f, r = 0.f;
            d.write(0.f, 0.f);
            d.read(&l, &r);
        }
        d.setDivision(kStartDiv);
    }
    for (int i = 0; i < 2000; i++)
    {
        float l = 0.f, r = 0.f;
        d.write(0.f, 0.f);
        d.read(&l, &r);
    }
    // a bar at 120 BPM: 96000 samples; the event's voice starts half of it back
    delayVoice& v = d.myVoices[1];
    v.setNextEvent(delayVoice::PITCH_UP, 0.f);
    v.startFadeIn();
    const float behind = Behind(v);
    printf("      pitch-up %s: starts %.0f samples back\n",
           turn_division ? "after a division change" : "right after Init", behind);
    Check(fabsf(behind - 48000.f) < 2.f, turn_division
              ? "pitch-up after a division change: half a bar back"
              : "pitch-up right after Init: half a bar back too");
}

int main()
{
    TestPitchUp(true);
    TestPitchUp(false);
    return Finish();
}
