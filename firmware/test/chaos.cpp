// chaos.cpp: checks the chaos key (FxChaos.h, #65): its dice on the grid of the FX clock, the
// pattern that repeats and the press that rolls a new one; the gates it puts on the engine's
// pool of latched effects (FxChain.h), and the loop's scramble (Looper::Scramble), which plays
// another part of the loop without moving its position, crossfaded. Exits 0 when everything
// passes. Run by unit.sh chaos.
#include <cmath>
#include <cstdio>
#include <vector>
#include "check.h"
#include "passthroughEngine.h"

using namespace chompi;

static const float kSr = 48000.f;
static const size_t kBlock = 24;

// ======== the dice ========

/** A bar of 16th pulses into chaos: each step's drops and jump */
struct Bar
{
    std::vector<uint16_t> drops;
    std::vector<float> jumps;
    bool operator==(const Bar& o) const { return drops == o.drops && jumps == o.jumps; }
};
static Bar PlayBar(Chaos& c, uint32_t& pos)
{
    Bar bar;
    for (uint32_t p = 0; p < kPulsesPerBar; p++, pos++)
    {
        c.ClockPulse(pos % kPulsesPerCycle);
        float jump;
        if (c.TakeStep(&jump))
        {
            bar.drops.push_back(c.Drops());
            bar.jumps.push_back(jump);
        }
    }
    return bar;
}

static void TestDice()
{
    Chaos c;
    c.Init();
    uint32_t pos = 0;
    c.SetParam(Chaos::FX_CHANCE, 1.f);
    c.SetParam(Chaos::SCRAMBLE, 1.f);
    c.SetParam(Chaos::GRID, 0.f); // 16ths
    Bar off = PlayBar(c, pos);
    Check(off.drops.empty(), "dice: off, no steps");

    c.SetOn(true);
    Bar bar = PlayBar(c, pos);
    Check(bar.drops.size() == 16, "dice: on a 1/16 grid, 16 steps a bar");

    // at the top: each effect sits out about half the steps, every step plays elsewhere
    int dropped = 0, steps = 0, elsewhere = 0;
    for (int b = 0; b < 50; b++)
    {
        bar = PlayBar(c, pos);
        for (size_t s = 0; s < bar.drops.size(); s++, steps++)
        {
            dropped += __builtin_popcount(bar.drops[s] & 0xfff);
            elsewhere += bar.jumps[s] > 0.f && bar.jumps[s] < 1.f;
        }
    }
    const float share = static_cast<float>(dropped) / (steps * 12);
    printf("      chances at the top: %.2f of the effects dropped a step, %d of %d steps elsewhere\n",
           share, elsewhere, steps);
    Check(share > .45f && share < .55f, "dice: FX chance at the top, each effect out half the steps");
    Check(elsewhere == steps, "dice: scramble chance at the top, every step elsewhere");

    c.SetParam(Chaos::FX_CHANCE, .2f);
    c.SetParam(Chaos::SCRAMBLE, 0.f);
    dropped = steps = elsewhere = 0;
    for (int b = 0; b < 50; b++)
    {
        bar = PlayBar(c, pos);
        for (size_t s = 0; s < bar.drops.size(); s++, steps++)
        {
            dropped += __builtin_popcount(bar.drops[s] & 0xfff);
            elsewhere += bar.jumps[s] != 0.f;
        }
    }
    const float rare = static_cast<float>(dropped) / (steps * 12);
    Check(rare > .07f && rare < .13f && elsewhere == 0,
          "dice: at a fifth, an effect drops out of 1 step in 10; scramble off, the loop in place");

    // the grid: a beat
    c.SetParam(Chaos::GRID, 2.f / 4.f);
    bar = PlayBar(c, pos);
    Check(bar.drops.size() == 4, "dice: on a 1/4 grid, 4 steps a bar");
    c.SetParam(Chaos::GRID, 1.f);
    bar = PlayBar(c, pos);
    Check(bar.drops.size() == 1, "dice: on a bar's grid, 1");

    // the pattern: at the right a bar rolled once repeats, at the left every bar is new
    c.SetParam(Chaos::GRID, 0.f);
    c.SetParam(Chaos::FX_CHANCE, .5f);
    c.SetParam(Chaos::SCRAMBLE, .5f);
    c.SetParam(Chaos::PATTERN, 1.f);
    const Bar first = PlayBar(c, pos);
    bool repeats = true;
    for (int b = 0; b < 8; b++)
        repeats = repeats && PlayBar(c, pos) == first;
    Check(repeats, "dice: pattern at the right, the bar repeats");
    c.SetParam(Chaos::PATTERN, 0.f);
    Check(!(PlayBar(c, pos) == first), "dice: at the left, it's new every bar");
    c.SetParam(Chaos::PATTERN, 1.f);
    const Bar kept = PlayBar(c, pos);
    Check(PlayBar(c, pos) == kept, "dice: back at the right, the last bar repeats");
    // a press (off, then on) rolls a fresh one
    c.SetOn(false);
    float jump;
    c.TakeStep(&jump);
    c.SetOn(true);
    const Bar fresh = PlayBar(c, pos);
    Check(!(fresh == kept) && PlayBar(c, pos) == fresh, "dice: a press rolls a fresh bar, which repeats");

    // off: nothing dropped, and once the loop back in place
    c.SetParam(Chaos::SCRAMBLE, 1.f);
    PlayBar(c, pos);
    PlayBar(c, pos);
    c.SetOn(false);
    Check(c.TakeStep(&jump) && jump == 0.f && c.Drops() == 0 && !c.TakeStep(&jump),
          "dice: off, nothing dropped, the loop in place once");
}

// ======== the gates, in the engine ========

static int16_t loop_mem[kLoopMemSize];
static const size_t kDelayFrames = 480000;
static float delay_mem[kDelayFrames * 2];
static const size_t kFreezerFrames = 240000;
static float freezer_l[kFreezerFrames], freezer_r[kFreezerFrames];
static daisysp::Reverb reverb;
static const size_t kTapeFrames = 1u << 19;
static float tape_l[kTapeFrames], tape_r[kTapeFrames];
static MidiClock midi_clock;
static PassthroughEngine engine;

static void Run(size_t blocks, uint16_t* seen = nullptr)
{
    float in[4][kBlock] = {}, out[4][kBlock];
    const float* ins[4] = {in[0], in[1], in[2], in[3]};
    float* outs[4] = {out[0], out[1], out[2], out[3]};
    for (size_t b = 0; b < blocks; b++)
    {
        engine.Process(ins, outs, kBlock);
        if (seen)
            *seen |= engine.FxDropped();
    }
}

static void TestGates()
{
    engine.Init(kSr, loop_mem, &midi_clock, delay_mem, kDelayFrames, &reverb, freezer_l,
                freezer_r, kFreezerFrames, tape_l, tape_r, kTapeFrames);
    const uint16_t filter = 1u << FX_FILTER, crusher = 1u << FX_CRUSHER;
    engine.SetFxOn(FX_FILTER, true);
    engine.SetFxOn(FX_CRUSHER, true);
    engine.SetFxParam(FX_CHAOS, Chaos::FX_CHANCE, 1.f);
    engine.SetFxParam(FX_CHAOS, Chaos::GRID, 0.f);
    uint16_t seen = 0;
    Run(4000, &seen);
    Check(seen == 0, "gates: chaos off, nothing dropped");
    engine.SetFxOn(FX_CHAOS, true);
    Run(4000, &seen);
    Check(seen == 0, "gates: chaos on, an empty pool, nothing dropped");

    // the filter in the pool (latched), the crusher held: only the filter drops out
    engine.SetFxPool(filter);
    seen = 0;
    uint16_t always = 0xffff;
    for (int b = 0; b < 4000; b++)
    {
        Run(1, &seen);
        always &= engine.FxDropped();
    }
    Check(seen == filter && always == 0, "gates: only the pool, now and then: the filter, not the crusher held");

    // dropped, the filter's key is off for its effect; the UI's stays on
    Run(1);
    while (!(engine.FxDropped() & filter))
        Run(1);
    Run(1000 / 2 / 24 + 1); // a 16th at 120 BPM is 125 ms; well inside it, faded down
    bool off_inside = false;
    for (int b = 0; b < 4000 && !off_inside; b++)
    {
        Run(1);
        off_inside = (engine.FxDropped() & filter) && engine.FxResting(FX_FILTER);
    }
    Check(off_inside, "gates: a dropped step fades the filter out until it comes back");

    // chaos off: the filter comes back on at once (faded in) and stays
    engine.SetFxOn(FX_CHAOS, false);
    seen = 0;
    Run(400, &seen);
    Check(seen == 0 && !engine.FxResting(FX_FILTER), "gates: chaos off, the pool back on");
    engine.SetFxOn(FX_FILTER, false);
    engine.SetFxOn(FX_CRUSHER, false);
    engine.SetFxPool(0);
}

// ======== the scramble, in the looper ========

static int16_t mem[kLoopMemSize];
static chompi::Looper looper;
static float Ramp(size_t n) { return static_cast<float>(n) / 48000.f * .5f; }

static void TestScramble()
{
    looper.Init(mem, &midi_clock);
    float in_l[kBlock], in_r[kBlock], out_l[kBlock], out_r[kBlock];
    size_t n = 0;
    auto block = [&] {
        for (size_t i = 0; i < kBlock; i++, n++)
            in_l[i] = in_r[i] = Ramp(n);
        looper.Process(in_l, in_r, out_l, out_r, kBlock);
    };
    looper.StartRecording(false);
    block();
    while (looper.GetState() == chompi::Looper::State::RECORDING && n < 48000)
        block();
    looper.StopRecording();
    block();
    const size_t length = looper.GetLength();
    for (int b = 0; b < 200; b++)
        block();
    Check(looper.GetState() == chompi::Looper::State::PLAYING && length > 0, "scramble: a loop of a ramp");

    // in place: the loop reads its position; scrambled, a quarter of the loop on, the position
    // running on as ever
    const float pos = looper.GetPosition();
    looper.Scramble(length / 4);
    float max_step = 0.f, last = out_l[kBlock - 1];
    for (int b = 0; b < 20; b++) // through the crossfade, 5 ms
    {
        block();
        for (size_t i = 0; i < kBlock; i++)
        {
            max_step = fmaxf(max_step, fabsf(out_l[i] - last));
            last = out_l[i];
        }
    }
    const float moved = looper.GetPosition() - pos;
    const float heard = out_l[kBlock - 1] / .5f;               // seconds into the ramp
    const float at = looper.GetPosition() * length / 48000.f;  // where the loop is, in seconds
    const float quarter = length / 4 / 48000.f;
    printf("      loop %.3f s at %.3f s, heard at %.3f s; largest step %.4f\n",
           length / 48000.f, at, heard, max_step);
    Check(fabsf(moved - 20.f * kBlock / length) < 1e-3f, "scramble: the loop's position runs on");
    Check(fabsf(heard - fmodf(at + quarter, length / 48000.f)) < .002f,
          "scramble: heard a quarter of the loop on from its position");
    // the jump is a crossfade from one part of the ramp to the other: no step bigger than its
    // share of the 5 ms
    Check(max_step < .5f * quarter / 240.f * 1.2f, "scramble: crossfaded over 5 ms, no jump");
    looper.Scramble(0);
    for (int b = 0; b < 20; b++)
        block();
    Check(fabsf(out_l[kBlock - 1] / .5f - looper.GetPosition() * length / 48000.f) < .002f,
          "scramble: back in place");
    looper.Scramble(length + 5);
    for (int b = 0; b < 20; b++)
        block();
    Check(fabsf(out_l[kBlock - 1] / .5f - looper.GetPosition() * length / 48000.f) < .002f,
          "scramble: past the loop's length, in place");
}

int main()
{
    TestDice();
    TestGates();
    TestScramble();
    return Finish();
}
