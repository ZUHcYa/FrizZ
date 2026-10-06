// FRIZZ engine harness: runs a fixed script of key presses and knob turns through
// PassthroughEngine on the host and writes every output sample and FX meter to a file, so two
// versions of the engine can be compared (see README.md). Built and run by run.sh.
//
// The script, one segment of 3s per FX plus four (14 with the folder) at 48kHz in 24-sample blocks:
//  - each FX on its own, on for 2.5s with a random knob turned every 0.25s
//  - the inserts together, then everything twice, then 3s of tails
// Every parameter starts at 0.5. The input is a 110Hz saw, a gated 2kHz sine and a little
// noise, all deterministic, as is the delay's rand() (seeded).
//
// Environment:
//  NOFX=1    never switches an FX on (shows whether a segment exercises its FX)
//  STRESS=1  everything on for the whole run, the resonator at full feedback with the
//            filter's resonance and the flanger's and shifter's feedback at the top
//
// Output, per block: 4 x 24 floats (headphone L/R, master L/R), then the FX meters in the
// order of kAll below: 9, or 10 from the folder on (compare.py tells them apart by the
// file's size).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "passthroughEngine.h"

static const float kSr = 48000.f;
static const size_t kBlock = 24;

static int16_t loop_mem[kLoopMemSize];
static const size_t kDelayFrames = 480000;
static float delay_mem[kDelayFrames * 2];
static const size_t kFreezerFrames = 240000;
static float freezer_mem_l[kFreezerFrames];
static float freezer_mem_r[kFreezerFrames];
static daisysp::Reverb reverb;
static chompi::MidiClock midi_clock;
static PassthroughEngine engine;

static uint32_t lcg = 12345;
static float Rnd()
{
    lcg = lcg * 1664525u + 1013904223u;
    return static_cast<float>(lcg >> 8) / 16777216.f;
}

// The inserts (and the resonator), then the two sends. The folder only where that version
// of the engine has it, so older versions still build and run as they did.
static const size_t kAll[] = {chompi::FX_FILTER,  chompi::FX_CRUSHER, chompi::FX_FREEZER,
                              chompi::FX_SLICER,  chompi::FX_FLANGER, chompi::FX_SHIFTER,
                              chompi::FX_RESONATOR,
#if __has_include("FxFolder.h")
                              chompi::FX_FOLDER,
#endif
                              chompi::FX_DELAY,   chompi::FX_REVERB};
static const size_t kNum = sizeof(kAll) / sizeof(kAll[0]);
static const size_t kNumInserts = kNum - 2;

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;
    FILE* f = fopen(argv[1], "wb");
    srand(1);

    engine.Init(kSr, loop_mem, &midi_clock, delay_mem, kDelayFrames, &reverb,
                freezer_mem_l, freezer_mem_r, kFreezerFrames);
    engine.SetMainGain(.75f);
    engine.SetInputGain(.75f);
    engine.SetFinalComp(.3f);
    engine.SetMix(0.f);
    // the defaults NormalPage pushes don't matter here: every parameter is set below
    for (size_t i = 0; i < kNum; i++)
        for (size_t p = 0; p < 4; p++)
            engine.SetFxParam(kAll[i], p, .5f);

    if (getenv("STRESS"))
    {
        // resonator at full feedback with the filter's resonance and the flanger's and
        // shifter's feedback at the top, all inside its loop, plus everything else on
        for (size_t i = 0; i < kNum; i++)
            engine.SetFxOn(kAll[i], true);
        engine.SetFxParam(chompi::FX_RESONATOR, 1, 1.f);
        engine.SetFxParam(chompi::FX_RESONATOR, 2, 1.f);
        engine.SetFxParam(chompi::FX_FILTER, 0, .2f);
        engine.SetFxParam(chompi::FX_FILTER, 1, 1.f);
        engine.SetFxParam(chompi::FX_FLANGER, 2, .5f);
        engine.SetFxParam(chompi::FX_FLANGER, 1, 1.f);
        engine.SetFxParam(chompi::FX_SHIFTER, 1, 1.f);
        engine.SetFxParam(chompi::FX_DELAY, 1, 1.f);
        engine.SetFxParam(chompi::FX_REVERB, 0, 1.f);
        engine.SetFxParam(chompi::FX_REVERB, 3, 1.f);
    }
    // per-fx solo segments of 3s, then 4s of all inserts, then 4s of everything, then tails
    const size_t seg = static_cast<size_t>(3.f * kSr / kBlock);
    const size_t total = (kNum + 4) * seg;
    const size_t knob_every = static_cast<size_t>(.25f * kSr / kBlock);

    float inl[kBlock], inr[kBlock], zero[kBlock] = {};
    float o0[kBlock], o1[kBlock], o2[kBlock], o3[kBlock];
    const float* in[4] = {zero, zero, inl, inr};
    float* out[4] = {o0, o1, o2, o3};
    double phase1 = 0, phase2 = 0;
    uint32_t noise = 777;

    for (size_t b = 0; b < total; b++)
    {
        const size_t s = b / seg, t = b % seg;
        bool on[kNum] = {};
        if (s < kNum)
            on[s] = t < seg * 5 / 6;
        else if (s < kNum + 1)
            for (size_t i = 0; i < kNumInserts; i++) on[i] = t < seg * 5 / 6;
        else if (s < kNum + 3)
            for (size_t i = 0; i < kNum; i++) on[i] = true;

        static bool was_on[kNum] = {};
        for (size_t i = 0; i < kNum; i++)
        {
            if (getenv("STRESS"))
                continue;
            if (on[i] != was_on[i] && !getenv("NOFX"))
                engine.SetFxOn(kAll[i], on[i]);
            was_on[i] = on[i];
            if (on[i] && t % knob_every == 0)
                engine.SetFxParam(kAll[i], static_cast<size_t>(Rnd() * 4.f) % 4, Rnd());
        }

        for (size_t i = 0; i < kBlock; i++)
        {
            // a 110Hz saw, a gated 2kHz sine, a little noise
            phase1 += 110.0 / kSr; if (phase1 >= 1) phase1 -= 1;
            phase2 += 2000.0 / kSr;
            noise = noise * 1103515245u + 12345u;
            const float n = (static_cast<float>(noise >> 9) / 4194304.f - 1.f) * .02f;
            const float gate = ((b / 400) % 2) ? 1.f : 0.f;
            const float saw = static_cast<float>(phase1 * 2 - 1) * .3f;
            const float sine = static_cast<float>(sin(2 * M_PI * phase2)) * .2f * gate;
            inl[i] = saw + sine + n;
            inr[i] = saw * .8f - sine + n;
        }
        engine.Process(in, out, kBlock);

        fwrite(o0, sizeof(float), kBlock, f);
        fwrite(o1, sizeof(float), kBlock, f);
        fwrite(o2, sizeof(float), kBlock, f);
        fwrite(o3, sizeof(float), kBlock, f);
        for (size_t i = 0; i < kNum; i++)
        {
            const float m = engine.GetFxLevel(kAll[i]);
            fwrite(&m, sizeof(float), 1, f);
        }
    }
    fclose(f);
    return 0;
}
