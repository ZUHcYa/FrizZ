// Clicks check: knob moves and key presses that used to jump the sound, on a 220Hz sine.
// Built and run by unit.sh clicks.
//  - the flanger's stereo knob turned back to 0 (as a recall or morph does): the right
//    channel's sweep glides back onto the left one instead of jumping there
#include <cmath>
#include "FxFlanger.h"
#include "check.h"

using namespace chompi;

static const float kSr = 48000.f;
// a 220Hz sine at .5 moves at most .0144 a sample; a flanger's sweep adds little to that
static const float kMaxStep = .05f;

static float Sine(size_t i, float hz = 220.f)
{
    return .5f * sinf(2.f * static_cast<float>(M_PI) * hz * static_cast<float>(i) / kSr);
}

/** Runs n samples of the sine through fx from sample i on; the largest step between two
 *  output samples, either channel */
template <class Fx>
static float Run(Fx& fx, size_t& i, size_t n)
{
    float step = 0.f, last_l = 0.f, last_r = 0.f;
    for (size_t k = 0; k < n; k++, i++)
    {
        float l = Sine(i), r = Sine(i);
        fx.Process(&l, &r);
        if (k > 0)
            step = fmaxf(step, fmaxf(fabsf(l - last_l), fabsf(r - last_r)));
        last_l = l;
        last_r = r;
    }
    return step;
}

static void TestFlanger()
{
    static chompi::Flanger fx;
    fx.Init(kSr);
    fx.SetParam(chompi::Flanger::RATE, .5f);
    fx.SetParam(chompi::Flanger::AMOUNT, .6f);
    fx.SetParam(chompi::Flanger::STEREO, .5f); // the right LFO free and detuned
    fx.SetOn(true);
    size_t i = 0;
    const float before = Run(fx, i, 3 * static_cast<size_t>(kSr)); // the LFOs drift apart
    fx.SetParam(chompi::Flanger::STEREO, 0.f);
    const float after = Run(fx, i, static_cast<size_t>(kSr));
    printf("      flanger: largest step %.4f in stereo, %.4f turning it to 0\n", before, after);
    Check(before < kMaxStep, "flanger: a stereo sweep is smooth");
    Check(after < kMaxStep, "flanger: stereo back to 0 glides, no jump");
}

int main()
{
    TestFlanger();
    return Finish();
}
