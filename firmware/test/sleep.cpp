// sleep.cpp: checks that an effect switched off stops costing time without changing what's
// heard (FxGate::Asleep, TailWatch in FxCommon.h): an insert off and faded out passes its
// input exactly, a send sleeps only once its tail has rung out, and both come back on their
// key. Exits 0 when everything passes. Run by unit.sh sleep.
#include <cmath>
#include <cstdio>
#include "check.h"
#include "FxChain.h"

using namespace chompi;

static const float kSr = 48000.f;

static float Sine(long n) { return .3f * sinf(2.f * 3.14159265f * 220.f * n / kSr); }

/** An insert, on for a while and then off: once faded out its output is its input, bit for
 *  bit, and it's Idle; on again, it shapes the sound again */
template <class Fx>
static void TestInsert(Fx& fx, const char* name)
{
    fx.Init(kSr);
    for (size_t p = 0; p < kNumFxParams; p++)
        fx.SetParam(p, .7f);
    fx.SetOn(true);
    long n = 0;
    for (; n < 24000; n++)
    {
        float l = Sine(n), r = Sine(n);
        fx.Process(&l, &r);
    }
    fx.SetOn(false);
    bool exact = true;
    for (long end = n + 48000; n < end; n++)
    {
        float l = Sine(n), r = Sine(n);
        const float in = l;
        fx.Process(&l, &r);
        if (n > end - 24000)
            exact = exact && l == in && r == in;
    }
    char what[96];
    snprintf(what, sizeof what, "%s: off, its output is its input exactly", name);
    Check(exact && fx.Idle(), what);

    fx.SetOn(true);
    bool shaped = false;
    for (long end = n + 24000; n < end; n++)
    {
        float l = Sine(n), r = Sine(n);
        const float in = l;
        fx.Process(&l, &r);
        shaped = shaped || fabsf(l - in) > 1e-3f;
    }
    snprintf(what, sizeof what, "%s: on again, it shapes the sound", name);
    Check(shaped && !fx.Idle(), what);
}

/** The resonator, whose loop wraps the inserts (Feed before them, Tap after): off and faded
 *  out, it passes its input exactly; on again, it rings */
static void TestResonator()
{
    static chompi::Resonator reso;
    reso.Init(kSr);
    reso.SetParam(chompi::Resonator::FEEDBACK, .9f);
    reso.SetOn(true);
    long n = 0;
    const auto Run = [&](long samples, bool* exact, bool* rings) {
        for (long end = n + samples; n < end; n++)
        {
            float l = Sine(n), r = Sine(n);
            const float in = l;
            reso.Feed(&l, &r);
            reso.Tap(l, r);
            if (exact && n > end - 24000)
                *exact = *exact && l == in && r == in;
            if (rings)
                *rings = *rings || fabsf(l - in) > 1e-3f;
        }
    };
    Run(24000, nullptr, nullptr);
    reso.SetOn(false);
    bool exact = true;
    Run(48000, &exact, nullptr);
    Check(exact && reso.Idle(), "resonator: off, its output is its input exactly");
    reso.SetOn(true);
    bool rings = false;
    Run(24000, nullptr, &rings);
    Check(rings && !reso.Idle(), "resonator: on again, it rings");
}

static float delay_mem[480000 * 2];

/** The delay: off, it keeps ringing out; it sleeps only after its whole buffer of silence,
 *  and wakes on its key */
static void TestDelay()
{
    static DelaySend delay;
    const size_t frames = 48000; // a 1s buffer, so the test is short
    delay.Init(delay_mem, frames);
    delay.SetTempo(120.f);
    delay.SetParam(DelaySend::DIVISION, .25f);
    delay.SetParam(DelaySend::FEEDBACK, .3f);
    delay.SetParam(DelaySend::LEVEL, 1.f);
    delay.SetOn(true);
    long n = 0;
    for (; n < 4800; n++)
    {
        float l = 0.f, r = 0.f;
        delay.Process(Sine(n), Sine(n), &l, &r);
    }
    delay.SetOn(false);
    bool rang = false;
    for (long end = n + 48000; n < end; n++) // the 1/4 at 120 BPM comes back after 0.5s
    {
        float l = 0.f, r = 0.f;
        delay.Process(Sine(n), Sine(n), &l, &r);
        rang = rang || fabsf(l) > 1e-3f;
    }
    Check(rang && !delay.Sleeping(), "delay: off, its echoes ring out");

    long slept_at = -1;
    for (long end = n + 20 * 48000; n < end && slept_at < 0; n++)
    {
        float l = 0.f, r = 0.f;
        delay.Process(Sine(n), Sine(n), &l, &r);
        if (delay.Sleeping())
            slept_at = n;
    }
    Check(slept_at >= 0, "delay: once rung out, it sleeps");

    // asleep it adds nothing
    float l = 0.f, r = 0.f;
    delay.Process(Sine(n), Sine(n), &l, &r);
    n++;
    Check(l == 0.f && r == 0.f, "delay: asleep, nothing added");

    delay.SetOn(true);
    bool echo = false;
    for (long end = n + 48000; n < end; n++)
    {
        float l = 0.f, r = 0.f;
        delay.Process(Sine(n), Sine(n), &l, &r);
        echo = echo || fabsf(l) > 1e-2f;
    }
    Check(echo && !delay.Sleeping(), "delay: on again, it echoes");
}

/** The reverb: the same, after 2s of silence */
static void TestReverb()
{
    static daisysp::Reverb engine;
    static ReverbSend reverb;
    reverb.Init(kSr, &engine);
    reverb.SetParam(ReverbSend::DECAY, .5f);
    reverb.SetParam(ReverbSend::LEVEL, 1.f);
    reverb.SetOn(true);
    long n = 0;
    for (; n < 4800; n++)
    {
        float l = 0.f, r = 0.f;
        reverb.Process(Sine(n), Sine(n), &l, &r);
    }
    reverb.SetOn(false);
    bool rang = false;
    for (long end = n + 4800; n < end; n++)
    {
        float l = 0.f, r = 0.f;
        reverb.Process(Sine(n), Sine(n), &l, &r);
        rang = rang || fabsf(l) > 1e-3f;
    }
    Check(rang && !reverb.Sleeping(), "reverb: off, its tail rings out");

    long slept_at = -1;
    for (long end = n + 60 * 48000; n < end && slept_at < 0; n++)
    {
        float l = 0.f, r = 0.f;
        reverb.Process(Sine(n), Sine(n), &l, &r);
        if (reverb.Sleeping())
            slept_at = n;
    }
    Check(slept_at >= 0, "reverb: once rung out, it sleeps");

    reverb.SetOn(true);
    bool wet = false;
    for (long end = n + 24000; n < end; n++)
    {
        float l = 0.f, r = 0.f;
        reverb.Process(Sine(n), Sine(n), &l, &r);
        wet = wet || fabsf(l) > 1e-2f;
    }
    Check(wet && !reverb.Sleeping(), "reverb: on again, it reverberates");
}

int main()
{
    static chompi::Shifter shifter;
    static chompi::Flanger flanger;
    static chompi::Warble warble;
    static chompi::Crusher crusher;
    static chompi::Filter filter;
    TestInsert(shifter, "shifter");
    TestInsert(flanger, "flanger");
    TestInsert(warble, "warble");
    TestInsert(crusher, "crusher");
    TestInsert(filter, "filter");
    TestResonator();
    TestDelay();
    TestReverb();
    return Finish();
}
