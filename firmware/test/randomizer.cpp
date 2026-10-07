// randomizer.cpp: checks the randomizer (FxRandomizer.h) on its clock: its patterns, chance,
// pulse width and shift, which effects a gate picks, and how FxChain hands them over and gives
// them back. Exits 0 when everything passes. Run by unit.sh randomizer.
#include <cmath>
#include <cstdio>
#include "check.h"
#include "FxChain.h"

using namespace chompi;

static const float kSr = 48000.f;
static const size_t kBlock = 24;
static const uint32_t kSixteenth = 6000;                    // samples at 120 BPM
static const uint32_t kPulse = kSixteenth / kPulsesPer16th; // samples per clock pulse

/** The randomizer at 120 BPM, a block at a time, with the clock's pulses on time */
struct Sim
{
    Randomizer r;
    uint32_t now = 0; // samples
    uint16_t manual = 0;

    void Init(float pattern, float width, float chance, float shift)
    {
        r.Init(kSr, kRandomPool);
        r.SetParam(Randomizer::PATTERN, pattern / 16.f);
        r.SetParam(Randomizer::WIDTH, width);
        r.SetParam(Randomizer::CHANCE, chance);
        r.SetParam(Randomizer::SHIFT, shift);
        now = 0;
        manual = 0;
    }
    /** One block: its pulses, then Block */
    void Step()
    {
        r.SetTempo(120);
        for (uint32_t s = now; s < now + kBlock; s++)
            if (s % kPulse == 0)
                r.ClockPulse(s / kPulse);
        r.Block(kBlock, manual);
        now += kBlock;
    }
};

/** The 16ths a bar fires on, as a pattern word */
static uint16_t FiredBar(Sim& sim)
{
    uint16_t fired = 0;
    const uint32_t start = sim.now;
    while (sim.now < start + 16 * kSixteenth)
    {
        const uint32_t before = sim.r.Fires();
        sim.Step();
        if (sim.r.Fires() != before)
            fired |= static_cast<uint16_t>(1u << (15 - (sim.now - kBlock - start) / kSixteenth));
    }
    return fired;
}

static void TestPatterns()
{
    bool all = true;
    for (size_t p = 0; p < Randomizer::kNumPatterns; p++)
    {
        Sim sim;
        sim.Init(static_cast<float>(p), .5f, 1.f, 0.f);
        sim.r.SetOn(true);
        const uint16_t first = FiredBar(sim), second = FiredBar(sim);
        if (first != kRandPatterns[p] || second != kRandPatterns[p])
        {
            printf("      pattern %zu: fired %04x %04x, wants %04x\n", p, first, second,
                   kRandPatterns[p]);
            all = false;
        }
    }
    Check(all, "patterns: at chance 1 every gate fires, each 16th on its own, nothing else");

    Sim sim;
    sim.Init(15, .5f, 0.f, 0.f);
    sim.r.SetOn(true);
    Check(FiredBar(sim) == 0, "chance 0: no gate");

    sim.Init(15, .5f, .5f, 0.f);
    sim.r.SetOn(true);
    int fired = 0;
    for (int bar = 0; bar < 50; bar++)
        fired += __builtin_popcount(FiredBar(sim));
    printf("      chance .5: %d of 400 gates\n", fired);
    Check(fired > 150 && fired < 250, "chance .5: about half the gates");

    sim.Init(2, .5f, 1.f, 0.f);
    Check(FiredBar(sim) == 0 && sim.r.Mask() == 0, "off: nothing fires");
}

/** Samples from the first gate's opening to its closing, and when it opened */
static void FirstGate(Sim& sim, uint32_t* opened, uint32_t* length)
{
    *opened = *length = 0;
    while (sim.now < 4 * kSixteenth && sim.r.Mask() == 0)
        sim.Step();
    *opened = sim.now - kBlock;
    while (sim.now < 8 * kSixteenth && sim.r.Mask() != 0)
        sim.Step();
    *length = sim.now - kBlock - *opened;
}

static void TestWidthAndShift()
{
    Sim sim;
    uint32_t opened, length;
    sim.Init(0, .5f, 1.f, 0.f);
    sim.r.SetOn(true);
    FirstGate(sim, &opened, &length);
    Check(opened == 0 && length >= 3000 - kBlock && length <= 3000 + kBlock,
          "pulse width .5: the gate is half a 16th, from the 16th's start");

    sim.Init(0, 1.f, 1.f, 0.f);
    sim.r.SetOn(true);
    FirstGate(sim, &opened, &length);
    Check(length >= kSixteenth - kBlock && length <= kSixteenth + kBlock,
          "pulse width 1: the gate is the whole 16th");

    sim.Init(0, 0.f, 1.f, 0.f);
    sim.r.SetOn(true);
    FirstGate(sim, &opened, &length);
    Check(length >= 960 && length <= 960 + kBlock, "pulse width 0: the gate is 20ms at least");

    sim.Init(0, .5f, 1.f, 1.f);
    sim.r.SetOn(true);
    FirstGate(sim, &opened, &length);
    Check(opened >= 3000 - kBlock && opened <= 3000 + kBlock,
          "shift 1: the gate comes half a 16th late");

    // every gate of a bar moved by the same
    sim.Init(12, .2f, 1.f, .5f);
    sim.r.SetOn(true);
    bool same = true;
    int gates = 0;
    while (sim.now < 16 * kSixteenth)
    {
        const uint32_t before = sim.r.Fires();
        sim.Step();
        if (sim.r.Fires() != before)
        {
            const uint32_t late = (sim.now - kBlock) % kSixteenth;
            same = same && late + kBlock > 1500 && late < 1500 + kBlock;
            gates++;
        }
    }
    Check(same && gates == 8, "shift .5: every gate a quarter of a 16th late");
}

static void TestPicks()
{
    Sim sim;
    sim.Init(15, 1.f, 1.f, 0.f);
    sim.manual = (1u << FX_FILTER) | (1u << FX_SHIFTER);
    sim.r.SetOn(true);
    bool count = true, pool = true, manual = true, fresh = true, values = true;
    int sizes[6] = {};
    uint16_t last = 0;
    uint32_t last_at = 0;
    int touching = 0;
    for (int bar = 0; bar < 40; bar++)
        for (uint32_t end = sim.now + 16 * kSixteenth; sim.now < end;)
        {
            sim.Step();
            const uint16_t taken = sim.r.Taken();
            if (!taken)
                continue;
            const int n = __builtin_popcount(taken);
            count = count && n >= 1 && n <= 5;
            if (n >= 1 && n <= 5)
                sizes[n]++;
            pool = pool && !(taken & ~kRandomPool);
            manual = manual && !(taken & sim.manual);
            // touching gates: the last one's effects are still fading out
            if (sim.now - last_at <= kSixteenth + kBlock)
            {
                fresh = fresh && !(taken & last);
                touching++;
            }
            last = taken;
            last_at = sim.now;
            for (size_t fx = 0; fx < kNumFx; fx++)
                if ((taken >> fx) & 1)
                    for (size_t p = 0; p < kNumFxParams; p++)
                        values = values && sim.r.Value(fx, p) >= 0.f && sim.r.Value(fx, p) < 1.f;
        }
    printf("      gates of 1-5 effects: %d %d %d %d %d\n", sizes[1], sizes[2], sizes[3], sizes[4],
           sizes[5]);
    Check(count && sizes[1] && sizes[5], "picks: 1 to 5 effects a gate, all of them happen");
    Check(pool, "picks: never a send, never the freezer");
    Check(manual, "picks: never an effect whose key is on");
    Check(fresh && touching > 100, "picks: never one the last gate had, still fading out");
    Check(values, "picks: every knob 0..1");

    sim.r.SetOn(false);
    sim.Step();
    Check(sim.r.Mask() == 0, "off: the gate closes at once");
}

static const size_t kDelayFrames = 480000;
static float delay_mem[kDelayFrames * 2];
static const size_t kFreezerFrames = 240000;
static float freezer_mem_l[kFreezerFrames], freezer_mem_r[kFreezerFrames];
static const size_t kTapeStopFrames = 1u << 19;
static float tapestop_mem_l[kTapeStopFrames], tapestop_mem_r[kTapeStopFrames];
static daisysp::Reverb reverb;
static FxChain chain;

/** One block through the chain, with the clock's pulses */
static uint32_t chain_now = 0;
static void ChainBlock()
{
    chain.SetTempo(120);
    for (uint32_t s = chain_now; s < chain_now + kBlock; s++)
        if (s % kPulse == 0)
            chain.ClockPulse(s / kPulse);
    chain.RandomBlock(kBlock);
    for (size_t i = 0; i < kBlock; i++)
    {
        float l = .1f * sinf(.01f * (chain_now + i)), r = l;
        chain.Process(&l, &r);
    }
    chain_now += kBlock;
}

static void TestChain()
{
    chain.Init(kSr, delay_mem, kDelayFrames, &reverb, freezer_mem_l, freezer_mem_r,
               kFreezerFrames, tapestop_mem_l, tapestop_mem_r, kTapeStopFrames);
    chain.SetRandomizerParam(Randomizer::PATTERN, 0.f); // one gate a bar
    chain.SetRandomizerParam(Randomizer::WIDTH, .5f);
    chain.SetRandomizerParam(Randomizer::CHANCE, 1.f);
    chain.SetRandomizerParam(Randomizer::SHIFT, 0.f);
    chain.SetOn(FX_CRUSHER, true);
    chain.SetRandomizerOn(true);
    ChainBlock();
    const uint16_t mask = chain.RandomMask();
    bool owned = mask != 0;
    for (size_t fx = 0; fx < kNumFx; fx++)
        owned = owned && chain.Owned(fx) == bool((mask >> fx) & 1);
    Check(owned, "chain: the gate's effects are the randomizer's");
    Check(!(mask & (1u << FX_CRUSHER)) && !chain.Owned(FX_CRUSHER), "chain: a key on stays the user's");

    // a key pressed on one the gate has: the user's at the next block
    size_t claimed = 0;
    while (!((mask >> claimed) & 1))
        claimed++;
    chain.SetOn(claimed, true);
    Check(chain.Owned(claimed), "chain: a key coming on is taken in the next block");
    ChainBlock();
    Check(!chain.Owned(claimed) && !((chain.RandomMask() >> claimed) & 1),
          "chain: a key coming on takes its effect back from the gate");
    chain.SetOn(claimed, false);

    // the gate closes, they fade out, then they're the user's again
    const uint16_t rest = static_cast<uint16_t>(mask & ~(1u << claimed));
    while (chain.RandomMask() && chain_now < kSixteenth)
        ChainBlock();
    bool fading = true;
    for (size_t fx = 0; fx < kNumFx; fx++)
        if ((rest >> fx) & 1)
            fading = fading && chain.Owned(fx);
    Check(fading, "chain: a closed gate's effects stay its while they fade out");
    for (uint32_t end = chain_now + 1500; chain_now < end;)
        ChainBlock();
    bool back = true;
    for (size_t fx = 0; fx < kNumFx; fx++)
        back = back && !chain.Owned(fx);
    Check(back, "chain: faded out, every effect is the user's again");

    chain.SetRandomizerOn(false);
    for (uint32_t end = chain_now + 16 * kSixteenth; chain_now < end;)
        ChainBlock();
    Check(chain.RandomMask() == 0 && chain.RandomFires() == 1, "chain: off, no more gates");

    // on again mid-bar, on a gate's 16th: it fires at once
    chain.SetRandomizerParam(Randomizer::PATTERN, 2.f / 16.f); // quarters
    while (chain_now % (4 * kSixteenth) != 4 * kBlock)
        ChainBlock();
    chain.SetRandomizerOn(true);
    ChainBlock();
    Check(chain.RandomFires() == 2, "chain: on again on a gate's 16th, the gate fires at once");

    // off during the shift's wait, on again on a 16th without a gate: nothing fires
    chain.SetRandomizerParam(Randomizer::SHIFT, 1.f);
    while (chain_now % (4 * kSixteenth) != 0)
        ChainBlock();
    ChainBlock(); // armed, half a 16th to wait
    chain.SetRandomizerOn(false);
    for (uint32_t end = chain_now + 2 * kSixteenth; chain_now < end;)
        ChainBlock();
    const uint32_t fires = chain.RandomFires();
    chain.SetRandomizerOn(true);
    for (uint32_t end = chain_now + kSixteenth; chain_now < end;)
        ChainBlock();
    Check(chain.RandomFires() == fires, "chain: a gate waiting when it went off doesn't fire later");
}

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
    g.Init(kSr, kRandomHeadroom);
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
    Check(LevelRun(g, 1.f, false, 24000) == 1.f, "level: back to exactly unity after the gate");
    Check(LevelRun(g, .5f, true, 24000) == 1.f, "level: a quieter output isn't turned up");

    // the chain: a sine at -12dBFS, the densest pattern, its gates the whole 16th, for 32
    // bars; no 16th comes out more than 3dB (+1dB for the attack) over what went in
    chain.Init(kSr, delay_mem, kDelayFrames, &reverb, freezer_mem_l, freezer_mem_r,
               kFreezerFrames, tapestop_mem_l, tapestop_mem_r, kTapeStopFrames);
    chain.SetRandomizerParam(Randomizer::PATTERN, 1.f);
    chain.SetRandomizerParam(Randomizer::WIDTH, 1.f);
    chain.SetRandomizerParam(Randomizer::CHANCE, 1.f);
    chain.SetRandomizerParam(Randomizer::SHIFT, 0.f);
    chain.SetRandomizerOn(true);
    float worst = 0.f;
    double in_sum = 0., out_sum = 0.;
    for (uint32_t s = 0; s < 32 * 16 * kSixteenth; s += kBlock)
    {
        chain.SetTempo(120);
        for (uint32_t p = s; p < s + kBlock; p++)
            if (p % kPulse == 0)
                chain.ClockPulse(p / kPulse);
        chain.RandomBlock(kBlock);
        for (uint32_t i = s; i < s + kBlock; i++)
        {
            float l = .25f * sinf(.01f * i), r = l;
            in_sum += 2. * l * l;
            chain.Process(&l, &r);
            out_sum += l * l + r * r;
        }
        if ((s + kBlock) % kSixteenth == 0)
        {
            worst = fmaxf(worst, static_cast<float>(10. * log10(out_sum / in_sum)));
            in_sum = out_sum = 0.;
        }
    }
    printf("  loudest 16th of 32 random bars: %+.1fdB\n", worst);
    Check(worst < 4.f, "level: no random 16th more than 3dB over the input");
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
    TestPatterns();
    TestWidthAndShift();
    TestPicks();
    TestChain();
    TestLevel();
    TestCrusherLevel();
    return Finish();
}
