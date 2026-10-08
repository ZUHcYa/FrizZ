// comp.cpp: checks the master compressor (MasterComp.h): off is an exact bypass, its static
// curve, its speed, no gain ripple on bass at its fastest, the linked stereo and the mix; the
// engine's safety limiter keeping the outputs within 1.0; and the master settings' file
// format (MasterSettings.h). Exits 0 when everything passes. Run by unit.sh comp.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "check.h"
#include "MasterSettings.h"
#include "passthroughEngine.h"

using namespace chompi;

static const float kSr = 48000.f;

static float Db(float x) { return 20.f * log10f(fabsf(x)); }

/** A compressor with its knobs set, settled */
static void Setup(MasterComp& c, float amount, float ratio, float speed, float mix)
{
    c.Init(kSr);
    c.SetParam(MasterComp::kAmount, amount);
    c.SetParam(MasterComp::kRatio, ratio);
    c.SetParam(MasterComp::kSpeed, speed);
    c.SetParam(MasterComp::kMix, mix);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 48000; i++)
    {
        l = r = 0.f;
        c.Process(&l, &r);
    }
}

/** The output level, dB, of a steady level in dB, once settled */
static float Steady(MasterComp& c, float in_db)
{
    const float in = powf(10.f, in_db / 20.f);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 96000; i++)
    {
        l = r = in;
        c.Process(&l, &r);
    }
    return Db(l);
}

static void TestBypass()
{
    MasterComp c;
    Setup(c, 0.f, .5f, .5f, 1.f);
    bool same = true;
    uint32_t n = 1;
    for (int i = 0; i < 48000; i++)
    {
        n = n * 1664525u + 1013904223u;
        const float x = (static_cast<float>(n >> 8) / 16777216.f - .5f) * 8.f;
        float l = x, r = -x * .5f;
        c.Process(&l, &r);
        same = same && l == x && r == -x * .5f;
    }
    Check(same, "amount 0: an exact bypass");

    Setup(c, 1.f, 1.f, .5f, 0.f);
    same = true;
    for (int i = 0; i < 4800; i++)
    {
        float l = 2.f, r = -2.f;
        c.Process(&l, &r);
        same = same && l == 2.f && r == -2.f;
    }
    Check(same, "mix 0: the dry signal, exactly");
}

static void TestCurve()
{
    MasterComp c;
    // amount 1: threshold -30dB; 4:1 reduces 0dB by 22.5dB, the makeup gives back half
    Setup(c, 1.f, .5f, .5f, 1.f);
    float out = Steady(c, 0.f);
    printf("      0dB in, amount 1, 4:1: %.2fdB out\n", out);
    Check(fabsf(out - -11.25f) < .2f, "amount 1, 4:1: 0dB comes out at -11.25dB");
    out = Steady(c, -40.f);
    Check(fabsf(out - (-40.f + 11.25f)) < .2f, "below the threshold: only the makeup");
    out = Steady(c, -30.f);
    // in the knee's middle: a quarter of the knee times the slope
    Check(fabsf(out - (-30.f + 11.25f - .75f * 6.f / 8.f)) < .2f, "at the threshold: the soft knee");

    Setup(c, 1.f, 1.f, .5f, 1.f);
    out = Steady(c, 0.f);
    Check(fabsf(out - -14.25f) < .2f, "20:1: 0dB comes out at -14.25dB");
    const float louder = Steady(c, 6.f);
    Check(louder - out < .5f, "20:1: 6dB louder in is under 0.5dB louder out");

    Setup(c, .5f, .5f, .5f, 1.f);
    out = Steady(c, 0.f);
    Check(fabsf(out - (-15.f * .75f + 7.5f * .75f)) < .2f, "amount .5: threshold -15dB");
}

/** ms until the reduction has recovered to 3dB after a loud burst stops */
static float Recovery(float speed)
{
    MasterComp c;
    Setup(c, 1.f, .5f, speed, 1.f);
    Steady(c, 0.f);
    for (int i = 0; i < 96000; i++)
    {
        float l = .01f, r = .01f;
        c.Process(&l, &r);
        if (c.GetReduction() > -3.f)
            return i / 48.f;
    }
    return 2000.f;
}

/** ms until the reduction reaches 10dB after a loud signal starts */
static float Grab(float speed)
{
    MasterComp c;
    Setup(c, 1.f, .5f, speed, 1.f);
    Steady(c, -60.f);
    for (int i = 0; i < 96000; i++)
    {
        float l = 1.f, r = 1.f;
        c.Process(&l, &r);
        if (c.GetReduction() < -10.f)
            return i / 48.f;
    }
    return 2000.f;
}

static void TestSpeed()
{
    const float r0 = Recovery(0.f), r5 = Recovery(.5f), r1 = Recovery(1.f);
    const float a0 = Grab(0.f), a5 = Grab(.5f), a1 = Grab(1.f);
    printf("      to 3dB after a burst: %.0f / %.0f / %.0fms; to 10dB on a step: %.2f / %.2f / %.2fms\n",
           r0, r5, r1, a0, a5, a1);
    Check(r0 < r5 && r5 < r1 && r1 < 2000.f, "speed: the release slows from fast to slow");
    Check(a0 < a5 && a5 < a1, "speed: the attack slows from fast to slow");
    Check(a0 < 1.f && a1 < 30.f, "attack: under 1ms fast, under 30ms slow");
}

static void TestLinked()
{
    MasterComp c;
    Setup(c, 1.f, .5f, .5f, 1.f);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 48000; i++)
    {
        l = 1.f;
        r = .01f;
        c.Process(&l, &r);
    }
    Check(fabsf(l / 1.f - r / .01f) < 1e-4f, "stereo linked: the quiet side gets the loud side's gain");
}

static int16_t loop_mem[kLoopMemSize];
static const size_t kDelayFrames = 480000;
static float delay_mem[kDelayFrames * 2];
static const size_t kFreezerFrames = 240000;
static float freezer_mem_l[kFreezerFrames], freezer_mem_r[kFreezerFrames];
static const size_t kTapeStopFrames = 1u << 19;
static float tapestop_mem_l[kTapeStopFrames], tapestop_mem_r[kTapeStopFrames];
static daisysp::Reverb reverb;
static MidiClock midi_clock;
static PassthroughEngine engine;

static void TestCeiling()
{
    // everything up: input, volume, the compressor's makeup, and a full-scale square that
    // starts from silence, so its first milliseconds come through before the reduction does
    engine.Init(kSr, loop_mem, &midi_clock, delay_mem, kDelayFrames, &reverb,
                freezer_mem_l, freezer_mem_r, kFreezerFrames,
                tapestop_mem_l, tapestop_mem_r, kTapeStopFrames);
    engine.SetMainGain(1.f);
    engine.SetInputGain(1.f);
    engine.SetCompParam(MasterComp::kAmount, 1.f);
    engine.SetCompParam(MasterComp::kRatio, 0.f);
    engine.SetCompParam(MasterComp::kSpeed, 1.f);
    engine.SetCompParam(MasterComp::kMix, 1.f);
    const size_t kBlock = 24;
    float zero[kBlock] = {}, inl[kBlock], inr[kBlock];
    float o[4][kBlock];
    const float* in[4] = {zero, zero, inl, inr};
    float* out[4] = {o[0], o[1], o[2], o[3]};
    float peak = 0.f;
    for (size_t b = 0; b < 48000 / kBlock; b++)
    {
        for (size_t i = 0; i < kBlock; i++)
        {
            const size_t n = b * kBlock + i;
            const float x = n < 12000 ? 0.f : ((n / 50) % 2 ? 1.f : -1.f);
            inl[i] = inr[i] = x;
        }
        engine.Process(in, out, kBlock);
        for (size_t c = 0; c < 4; c++)
            for (size_t i = 0; i < kBlock; i++)
                peak = fmaxf(peak, fabsf(o[c][i]));
    }
    printf("      peak out, everything up: %.4f\n", peak);
    Check(peak <= 1.f && peak > .5f, "safety limiter: the outputs stay within 1.0");
}

static void TestFile()
{
    MasterSettings a, b;
    a.Reset();
    a.comp[0] = .3f;
    a.comp[1] = .75f;
    a.comp[2] = 0.f;
    a.comp[3] = .123457f;
    char buf[kMasterFileMax];
    const size_t len = FormatMaster(a, buf, sizeof(buf));
    printf("%s", buf);
    bool same = ParseMaster(buf, b) && len > 0;
    for (size_t p = 0; p < kNumFxParams; p++)
        same = same && fabsf(a.comp[p] - b.comp[p]) < 1e-6f;
    Check(same, "file: round-trips");

    // the coarse ratio's points come back on the grid
    a.comp[1] = .25f;
    FormatMaster(a, buf, sizeof(buf));
    ParseMaster(buf, b);
    Check(b.comp[1] == .25f, "file: a grid point comes back exactly");

    Check(!ParseMaster("FRIZZ scenes 1\n", b) && b.comp[3] == kCompParams.defaults[3],
          "file: another file isn't read, the defaults stay");
    Check(!ParseMaster("FRIZZ master 10\n", b), "file: another version isn't read");
    Check(ParseMaster("FRIZZ master 1\r\nvolume 3\r\ncompressor 500000 2000000\r\n", b) &&
              b.comp[0] == .5f && b.comp[1] == 1.f && b.comp[2] == kCompParams.defaults[2],
          "file: unknown lines skipped, values clamped, missing ones on their defaults");
    Check(ParseMaster("FRIZZ master 1\n", b) && b.comp[0] == kCompParams.defaults[0],
          "file: an empty one, every default");
    Check(FormatMaster(a, buf, 20) == 0, "file: one that doesn't fit isn't written");

    // the randomizer's knobs, on a line of their own
    a.Reset();
    a.rand[0] = 15.f / 16.f;
    a.rand[3] = .3f;
    FormatMaster(a, buf, sizeof(buf));
    same = ParseMaster(buf, b);
    for (size_t p = 0; p < kNumFxParams; p++)
        same = same && fabsf(a.rand[p] - b.rand[p]) < 1e-6f && fabsf(a.comp[p] - b.comp[p]) < 1e-6f;
    Check(same, "file: the randomizer's knobs round-trip");
    Check(ParseMaster("FRIZZ master 1\ncompressor 500000 500000 500000 500000\n", b) &&
              b.comp[0] == .5f && b.rand[0] == kRandParams.defaults[0] &&
              b.rand[2] == kRandParams.defaults[2],
          "file: one from before the randomizer, its knobs on their defaults");

    // the mono input switch, on a line of its own
    a.Reset();
    a.mono = true;
    FormatMaster(a, buf, sizeof(buf));
    Check(ParseMaster(buf, b) && b.mono, "file: mono round-trips");
    a.mono = false;
    FormatMaster(a, buf, sizeof(buf));
    Check(ParseMaster(buf, b) && !b.mono, "file: stereo round-trips");
    Check(ParseMaster("FRIZZ master 1\ncompressor 500000 500000 500000 500000\n", b) && !b.mono,
          "file: one from before the mono switch, stereo");
}

/** The gain's ripple, dB, on a steady sine of freq Hz at 0dB: max minus min reduction
 *  over a second, once settled */
static float Ripple(float freq, float speed)
{
    MasterComp c;
    Setup(c, 1.f, 1.f, speed, 1.f);
    float lo = 0.f, hi = -100.f;
    for (int i = 0; i < 96000; i++)
    {
        float l = sinf(2.f * static_cast<float>(M_PI) * freq * i / kSr), r = l;
        c.Process(&l, &r);
        if (i >= 48000)
        {
            lo = fminf(lo, c.GetReduction());
            hi = fmaxf(hi, c.GetReduction());
        }
    }
    return hi - lo;
}

static void TestRipple()
{
    const float r50 = Ripple(50.f, 0.f), r100 = Ripple(100.f, 0.f);
    printf("      gain ripple at 20:1, fast, on a 0dB sine: %.2fdB at 50Hz, %.2fdB at 100Hz\n",
           r50, r100);
    Check(r50 < 1.f && r100 < 1.f, "fast: the gain doesn't follow a bass note's waveform");
}

int main()
{
    TestBypass();
    TestCurve();
    TestSpeed();
    TestRipple();
    TestLinked();
    TestCeiling();
    TestFile();
    return Finish();
}
