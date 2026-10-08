// Clicks check: knob moves and key presses that used to jump the sound, on a 220Hz sine.
// Built and run by unit.sh clicks.
//  - the flanger's stereo knob turned back to 0 (as a recall or morph does): the right
//    channel's sweep glides back onto the left one instead of jumping there
//  - the freezer pressed again while its release fades the loop out: the loop plays on and
//    hands over to the new capture, instead of dropping to the live signal at once
#include <cmath>
#include <vector>
#include "FxFlanger.h"
#include "FxFreezer.h"
#include "check.h"

using namespace chompi;

static const float kSr = 48000.f;
// a 220Hz sine at .5 moves at most .0144 a sample; a flanger's sweep adds little to that
static const float kMaxStep = .05f;

static float Sine(size_t i, float hz = 220.f)
{
    return .5f * sinf(2.f * static_cast<float>(M_PI) * hz * static_cast<float>(i) / kSr);
}

// what the FX's clock pulses: 12 PPQN at 120 BPM
static const size_t kPulseSamples = 2000;

static void NoPulse(size_t) {}

// the last output, so a step across two Runs (a press between them) counts too
static float last_l = 0.f, last_r = 0.f;

/** Runs n samples of the sine through fx from sample i on, with pulse(position) on every
 *  clock pulse; the largest step between two output samples, either channel, the one from
 *  the last Run's end included unless i is 0 */
template <class Fx, class Pulse = void (*)(size_t)>
static float Run(Fx& fx, size_t& i, size_t n, Pulse pulse = NoPulse)
{
    float step = 0.f;
    for (size_t k = 0; k < n; k++, i++)
    {
        if (i % kPulseSamples == 0)
            pulse(i / kPulseSamples);
        float l = Sine(i), r = Sine(i);
        fx.Process(&l, &r);
        if (i > 0)
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

static void TestFreezer()
{
    static const size_t kFrames = 240000;
    static std::vector<float> buf_l(kFrames), buf_r(kFrames);
    static chompi::Freezer fx;
    fx.Init(kSr, buf_l.data(), buf_r.data(), kFrames);
    fx.SetParam(chompi::Freezer::LENGTH, 1.f / 7.f); // 1/8T, which a 220Hz cycle doesn't fit
    fx.SetTempo(120.f);
    auto pulse = [](size_t pos) { fx.ClockPulse(static_cast<uint32_t>(pos)); };
    size_t i = 1; // off the 16th, so the press waits for one
    last_l = last_r = Sine(0);
    fx.SetOn(true);
    // 1.1 s: the loop isn't in phase with the live sine (220Hz fits 1 s exactly)
    const float held = Run(fx, i, static_cast<size_t>(1.1f * kSr), pulse);
    fx.SetOn(false);
    Run(fx, i, 16, pulse); // into the release, where the loop and the live sine differ
    fx.SetOn(true);
    const float again = Run(fx, i, static_cast<size_t>(kSr / 2), pulse);
    printf("      freezer: largest step %.4f repeating, %.4f pressed again in the release\n", held,
           again);
    Check(held < kMaxStep, "freezer: repeating is smooth");
    Check(again < kMaxStep, "freezer: pressed again in the release, no drop to the live signal");
}

int main()
{
    TestFlanger();
    TestFreezer();
    return Finish();
}
