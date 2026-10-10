// level.cpp: checks page 2's shared knobs (FxOutput.h), Mix, Band and Level, around real
// effects: at their defaults the effect runs exactly as without them, an effect that's off is
// untouched by them, Level's gains and mute, Band splitting the effect off to the lows or the
// highs and putting the rest back exactly, a send's Band; and that nothing turns the crusher
// down by itself any more. Exits 0 when everything passes. Run by unit.sh level.
#include <cmath>
#include <cstdio>
#include "check.h"
#include "FxChain.h"

using namespace chompi;

static const float kSr = 48000.f;

/** A noise sample, -1..1, the same sequence every run */
static float Noise(uint32_t& n)
{
    n = n * 1664525u + 1013904223u;
    return static_cast<float>(n >> 8) / 8388608.f - 1.f;
}

static void TestDefaults()
{
    // two crushers alike, one alone and one inside an FxOutput at its defaults
    static Crusher a, b;
    FxOutput out;
    for (Crusher* c : {&a, &b})
    {
        c->Init(kSr);
        c->SetParam(Crusher::RATE, .5f);
        c->SetParam(Crusher::BITS, .6f);
        c->SnapParams();
        c->SetOn(true);
    }
    out.Init(kSr);
    out.SetParam(FxOutput::kMix, FxOutput::kMixDefault);
    out.SetParam(FxOutput::kBand, FxOutput::kBandDefault);
    out.SetParam(FxOutput::kLevel, FxOutput::kLevelDefault);
    bool same = true;
    uint32_t n = 1;
    for (size_t i = 0; i < 24000; i++)
    {
        const float x = .5f * Noise(n);
        float l1 = x, r1 = -x, l2 = x, r2 = -x;
        a.Process(&l1, &r1);
        out.Process(b, &l2, &r2);
        same = same && l1 == l2 && r1 == r2;
    }
    Check(same, "defaults: the effect runs exactly as without them, bit for bit");

    // off and faded out, whatever they're set to: the input passes untouched
    out.SetParam(FxOutput::kMix, .3f);
    out.SetParam(FxOutput::kBand, .2f);
    out.SetParam(FxOutput::kLevel, .4f);
    b.SetOn(false);
    same = true;
    for (size_t i = 0; i < 48000; i++)
    {
        const float x = .5f * Noise(n);
        float l = x, r = -x;
        out.Process(b, &l, &r);
        if (i >= 4800)
            same = same && l == x && r == -x;
    }
    Check(same, "off: the input passes untouched, bit for bit, whatever page 2 says");
}

/** The level in dB of what a shifter an octave up puts out on a 220Hz sine at Level val,
 *  over the last half second */
static float ShifterLevel(float val)
{
    static Shifter sh;
    FxOutput out;
    sh.Init(kSr);
    sh.SetParam(Shifter::SHIFT, 1.f);
    sh.SnapParams();
    sh.SetOn(true);
    out.Init(kSr);
    out.SetParam(FxOutput::kLevel, val);
    out.Snap();
    double sum = 0.;
    for (size_t i = 0; i < 48000; i++)
    {
        float l = .25f * sinf(2.f * float(M_PI) * 220.f * i / kSr), r = l;
        out.Process(sh, &l, &r);
        if (i >= 24000)
            sum += l * l;
    }
    return static_cast<float>(10. * log10(sum / 24000. + 1e-30));
}

static void TestLevel()
{
    Check(FxOutput::LevelGain(FxOutput::kLevelDefault) == 1.f && FxOutput::LevelGain(0.f) == 0.f,
          "level: 0dB at 3/4 exactly, mute at 0");
    Check(fabsf(20.f * log10f(FxOutput::LevelGain(1.f)) - 12.f) < .01f
              && fabsf(20.f * log10f(FxOutput::LevelGain(.125f)) + 30.f) < .01f,
          "level: +12dB at the top, 6dB a coarse step");
    const float unity = ShifterLevel(.75f), down = ShifterLevel(.5f), up = ShifterLevel(1.f);
    printf("  level: an octave up at 0dB %.1fdB, at 1/2 %.1fdB, at the top %.1fdB\n", unity, down,
           up);
    Check(fabsf(down - unity + 12.f) < .1f && fabsf(up - unity - 12.f) < .1f,
          "level: on an effect, -12dB at 1/2 and +12dB at the top");
    const float muted = ShifterLevel(0.f);
    printf("  level: at 0 %.1fdB\n", muted);
    Check(muted < -200.f, "level: at 0 the effect is muted");
}

/** How much of a sine at freq Hz comes out of a shifter an octave up muted by Level, with
 *  Band at band: the amplitude of what's left at freq, 0..1 */
static float BandPasses(float band, float freq)
{
    static Shifter sh;
    FxOutput out;
    sh.Init(kSr);
    sh.SetParam(Shifter::SHIFT, 1.f);
    sh.SnapParams();
    sh.SetOn(true);
    out.Init(kSr);
    out.SetParam(FxOutput::kBand, band);
    out.SetParam(FxOutput::kLevel, 0.f);
    out.Snap();
    double re = 0., im = 0.;
    for (size_t i = 0; i < 48000; i++)
    {
        const float ph = 2.f * float(M_PI) * freq * i / kSr;
        float l = .25f * sinf(ph), r = l;
        out.Process(sh, &l, &r);
        if (i >= 24000)
        {
            re += l * sinf(ph);
            im += l * cosf(ph);
        }
    }
    return static_cast<float>(2. * sqrt(re * re + im * im) / 24000. / .25);
}

static void TestBand()
{
    // the effect muted: what comes out is the rest of the spectrum the Band left it
    const float all_lo = BandPasses(.5f, 100.f), all_hi = BandPasses(.5f, 6000.f);
    const float hi_lo = BandPasses(.9f, 100.f), hi_hi = BandPasses(.9f, 6000.f);
    const float lo_lo = BandPasses(.15f, 100.f), lo_hi = BandPasses(.15f, 6000.f);
    printf("  band: muted effect, what's left of 100Hz / 6kHz: all %.2f / %.2f, highs %.2f / %.2f, "
           "lows %.2f / %.2f\n",
           all_lo, all_hi, hi_lo, hi_hi, lo_lo, lo_hi);
    Check(all_lo < 1e-3f && all_hi < 1e-3f, "band: in the middle the effect gets everything");
    Check(hi_lo > .95f && hi_hi < .5f, "band: turned right, the effect gets the highs, the lows pass dry");
    Check(lo_hi > .95f && lo_lo < .5f, "band: turned left, the effect gets the lows, the highs pass dry");

    // Mix at 0 with the Band on: the band comes back as it went, so the whole signal does
    static Shifter sh;
    FxOutput out;
    sh.Init(kSr);
    sh.SetParam(Shifter::SHIFT, 1.f);
    sh.SnapParams();
    sh.SetOn(true);
    out.Init(kSr);
    out.SetParam(FxOutput::kBand, .8f);
    out.SetParam(FxOutput::kMix, 0.f);
    out.Snap();
    float worst = 0.f;
    uint32_t n = 7;
    for (size_t i = 0; i < 24000; i++)
    {
        const float x = .5f * Noise(n);
        float l = x, r = x;
        out.Process(sh, &l, &r);
        worst = fmaxf(worst, fabsf(l - x));
    }
    Check(worst < 1e-6f, "band: the two parts add up to the signal");
}

static void TestSendBand()
{
    // a send's input, turned right: the highs go in, the lows don't
    FxOutput out;
    out.Init(kSr);
    out.SetParam(FxOutput::kBand, .9f);
    out.Snap();
    double lo = 0., hi = 0.;
    for (size_t i = 0; i < 48000; i++)
    {
        float l = .25f * sinf(2.f * float(M_PI) * 60.f * i / kSr), r = l;
        out.Band(&l, &r);
        if (i >= 24000)
            lo += l * l;
    }
    FxOutput out2;
    out2.Init(kSr);
    out2.SetParam(FxOutput::kBand, .9f);
    out2.Snap();
    for (size_t i = 0; i < 48000; i++)
    {
        float l = .25f * sinf(2.f * float(M_PI) * 8000.f * i / kSr), r = l;
        out2.Band(&l, &r);
        if (i >= 24000)
            hi += l * l;
    }
    const float lo_db = static_cast<float>(10. * log10(lo / 24000. / (.25 * .25 / 2)));
    const float hi_db = static_cast<float>(10. * log10(hi / 24000. / (.25 * .25 / 2)));
    printf("  send band, turned right: 60Hz %.1fdB, 8kHz %.1fdB\n", lo_db, hi_db);
    Check(lo_db < -25.f && hi_db > -3.f, "send band: turned right, a low cut on what goes in");
}

/** The crusher with XOR at full on a sine: the output's level over the input's, in dB, over
 *  the last half of a second */
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
    const float quiet = CrusherOver(.01f);
    printf("  crusher, XOR full: %+.1fdB at -40dBFS\n", quiet);
    Check(quiet > 10.f, "crusher: XOR on a quiet signal is louder than it: nothing turns it down "
                        "(page 2's Level is for that)");
}

int main()
{
    TestDefaults();
    TestLevel();
    TestBand();
    TestSendBand();
    TestCrusherLevel();
    return Finish();
}
