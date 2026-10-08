// level.cpp: checks the level guard (LevelGuard, FxCommon.h) on its own and the crusher's,
// which holds XOR to the input's level. Exits 0 when everything passes. Run by unit.sh level.
#include <cmath>
#include <cstdio>
#include "check.h"
#include "FxChain.h"

using namespace chompi;

static const float kSr = 48000.f;

/** LevelGuard alone: a steady input, an output some gain over it */
static float LevelRun(LevelGuard& g, float over, bool active, size_t samples)
{
    float l = 0.f;
    for (size_t i = 0; i < samples; i++)
    {
        const float in = .25f * sinf(.01f * i);
        l = in * over;
        float r = l;
        g.Process(in, in, &l, &r, active);
    }
    return g.Gain();
}

static void TestLevel()
{
    LevelGuard g;
    g.Init(kSr, 1.4125f); // +3dB of headroom
    bool same = true;
    for (size_t i = 0; i < 48000; i++)
    {
        const float in = .25f * sinf(.01f * i);
        float l = 4.f * in, r = -l;
        g.Process(in, in, &l, &r, false);
        same = same && l == 4.f * in && r == -4.f * in;
    }
    Check(same, "level: off, the output is untouched, bit for bit");

    const float held = LevelRun(g, 4.f, true, 24000) * 4.f; // 12dB over
    Check(fabsf(20.f * log10f(held) - 3.f) < .5f, "level: 12dB over is held to +3dB");
    Check(LevelRun(g, 1.f, false, 24000) == 1.f, "level: back to exactly unity once inactive");
    Check(LevelRun(g, .5f, true, 24000) == 1.f, "level: a quieter output isn't turned up");
}

/** The crusher's guard: XOR at full on a sine; the output's level over the input's, in dB,
 *  over the last half of a second */
static float CrusherOver(float amp)
{
    Crusher c;
    c.Init(kSr);
    c.SetParam(Crusher::RATE, 0.f);
    c.SetParam(Crusher::BITS, 0.f);
    c.SetParam(Crusher::TONE, 1.f);
    c.SetParam(Crusher::XOR, 1.f);
    c.SnapParams();
    c.SetOn(true);
    double in_sum = 0., out_sum = 0.;
    for (size_t i = 0; i < 48000; i++)
    {
        float l = amp * sinf(.01f * i), r = l;
        const float in = l;
        c.Process(&l, &r);
        if (i >= 24000)
        {
            in_sum += 2. * in * in;
            out_sum += l * l + r * r;
        }
    }
    return static_cast<float>(10. * log10(out_sum / in_sum));
}

static void TestCrusherLevel()
{
    const float quiet = CrusherOver(.01f), loud = CrusherOver(.5f);
    printf("  crusher, XOR full: %+.1fdB at -40dBFS, %+.1fdB at -6dBFS\n", quiet, loud);
    Check(quiet < .5f, "crusher: XOR on a quiet signal is no louder than it");
    Check(loud < .5f && loud > -6.f, "crusher: XOR on a loud one is no louder, and still there");
}

int main()
{
    TestLevel();
    TestCrusherLevel();
    return Finish();
}
