// tempo.cpp: checks the FX's tempo on the host: tap tempo (TapTempo.h), fitting and guessing a
// loop's beats, the tempo clock locked to a loop (TempoClock.h) and a quantized loop's beats
// (Looper.h), with a faked MIDI clock where it matters, and a scene morph landing on the
// clock's bar lines (FxMorph.h). Exits 0 when everything passes. Run by tempo.sh.
#include <cmath>
#include <cstdio>
#include <vector>
#include "FxMorph.h"
#include "Looper.h"
#include "TapTempo.h"
#include "TempoClock.h"

using namespace chompi;

static int failures = 0;

static void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static const float kSr = 48000.f;
static const size_t kBlock = 24;

static void TestTaps()
{
    TapTempo tap;
    Check(!tap.Tap(1000) && !tap.Tap(1500), "taps: none from 1 or 2 taps");
    Check(tap.Tap(2000) && fabsf(tap.Bpm() - 120.f) < .01f, "taps: 3 at 0.5s give 120 BPM");
    tap.Tap(2600);
    Check(fabsf(tap.Bpm() - 60000.f / (1600.f / 3.f)) < .01f, "taps: the mean of the intervals");
    for (uint32_t t = 3000; t <= 6000; t += 400)
        tap.Tap(t);
    Check(fabsf(tap.Bpm() - 150.f) < .01f, "taps: only the last 4 intervals count");
    Check(!tap.Tap(9000), "taps: a gap over 2s starts over");
    tap.Tap(9300);
    Check(tap.Tap(9600) && fabsf(tap.Bpm() - 200.f) < .01f, "taps: and counts again from there");
}

static void TestFit()
{
    Check(GuessBeats(2 * 48000, kSr) == 4, "guess: 2s is 4 beats, 120 BPM");
    Check(GuessBeats(3 * 48000, kSr) == 4, "guess: 3s is 4 beats, 80 BPM");
    Check(GuessBeats(static_cast<size_t>(3.1f * 48000), kSr) == 8, "guess: 3.1s is 8 beats, 155 BPM");
    Check(GuessBeats(16 * 48000, kSr) == 32, "guess: 16s is 32 beats, 120 BPM");
    Check(GuessBeats(24000, kSr) == 1, "guess: a short loop is one beat");
    Check(FitBeats(static_cast<size_t>(1.5f * 48000), kSr, 120.f) == 3, "fit: 1.5s at 120 is 3 beats");
    Check(FitBeats(2 * 48000, kSr, 63.f) == 2, "fit: 2s near 60 is 2 beats");
    Check(FitBeats(2 * 48000, kSr, 10.f) == 1, "fit: at least 1 beat");
}

/** The pulse positions a block gives */
static std::vector<uint32_t> Pulses(TempoClock& clock, float pos, float speed, bool paused = false)
{
    std::vector<uint32_t> out;
    const uint32_t n = clock.Process(kBlock, pos, speed, paused);
    for (uint32_t i = 0; i < n; i++)
        out.push_back(clock.Pulse());
    return out;
}

static void TestLoopClock()
{
    MidiClock midi;
    TempoClock clock;
    clock.Init(kSr, &midi);
    Check(!clock.TempoSet() && clock.GetTempo() == kDefaultBpm, "free: 120 BPM, no tempo set");

    // a 2s loop of 4 beats: 48 pulses a pass, 120 BPM
    const size_t length = 2 * 48000;
    clock.SetLoop(length, 4);
    std::vector<uint32_t> p = Pulses(clock, 0.f, 1.f);
    Check(p.size() == 1 && p[0] == 0, "loop: beat 1 on the loop's start");
    Check(clock.GetTempo() == 120, "loop: its tempo");

    // one pass, a block at a time
    uint32_t count = 0, last = 0;
    bool ascending = true;
    for (size_t f = kBlock; f < length; f += kBlock)
        for (uint32_t pos : Pulses(clock, static_cast<float>(f) / length, 1.f))
        {
            ascending = ascending && pos == last + 1;
            last = pos;
            count++;
        }
    Check(count == 47 && last == 47 && ascending, "loop: 48 pulses a pass, counting up");
    p = Pulses(clock, 0.f, 1.f);
    Check(p.size() == 1 && p[0] == 0, "loop: the next pass starts on 0 again");

    // half speed: half the tempo; reverse: counting down across the loop point
    Pulses(clock, 0.f, .5f);
    Check(clock.GetTempo() == 60, "loop: half speed, half the tempo");
    p = Pulses(clock, .99f, -1.f);
    Check(p.size() == 1 && p[0] == 0 && clock.Reverse(), "loop: reverse, beat 1 as it crosses the loop point");
    p = Pulses(clock, 46.5f / 48.f, -1.f);
    Check(p.size() == 1 && p[0] == 47, "loop: reverse, each pulse where forward has it");
    p = Pulses(clock, 47.5f / 48.f, 1.f);
    Check(p.size() == 1 && p[0] == 47, "loop: and forward again over the same spot, the same pulse");
    Check(Pulses(clock, 47.5f / 48.f, -1.f).empty(), "loop: standing still, no pulses");
    Check(clock.GetTempo() == 120, "loop: reverse, the tempo of the speed's size");

    // paused at 10.5 pulses in: the grid runs on at the loop's tempo, 24 pulses a second,
    // counting up from where it stopped; scrubbing doesn't move it
    clock.SetLoop(length, 4);
    Pulses(clock, 0.f, 1.f);
    Pulses(clock, 10.5f / 48.f, 1.f);
    std::vector<uint32_t> paused;
    for (size_t f = 0; f < 48000; f += kBlock)
        for (uint32_t pos : Pulses(clock, (10.5f + f / 48000.f) / 48.f, 1.f, true))
            paused.push_back(pos);
    bool on = paused.size() >= 23 && paused.size() <= 25 && paused[0] == 11;
    for (size_t i = 1; on && i < paused.size(); i++)
        on = paused[i] == (paused[i - 1] + 1) % 48;
    Check(on && !clock.Reverse(), "paused: the grid runs on at the tempo, from where it stopped");
    Check(clock.GetTempo() == 120, "paused: the loop's tempo");
    p = Pulses(clock, 30.5f / 48.f, 1.f);
    Check(p.empty(), "resumed: no burst of pulses for the jump");
    p = Pulses(clock, 31.2f / 48.f, 1.f);
    Check(p.size() == 1 && p[0] == 31, "resumed: the next pulse on the loop's position");

    // a tap at double tempo: 8 beats, 96 pulses a pass
    clock.Tap(235.f);
    Check(clock.TempoSet(), "loop tap: a tempo is set");
    Pulses(clock, 0.f, 1.f);
    Check(clock.GetTempo() == 240, "loop tap: the loop refitted to 8 beats, 240 BPM");
    p = Pulses(clock, 1.f / 96.f + 1e-4f, 1.f);
    Check(p.size() == 1 && p[0] == 1, "loop tap: 96 pulses a pass");

    // a long loop: the position still wraps at the cycle
    clock.SetLoop(length, 4);
    clock.ClearLoop();
    Check(!clock.HasLoop() && clock.GetTempo() == 120, "erased: free at the loop's tempo");
    clock.SetLoop(16 * 48000, 32);
    Pulses(clock, 0.f, 1.f);
    Pulses(clock, 100.5f / 384.f, 1.f); // under half a pass per block, as any real block
    p = Pulses(clock, 200.5f / 384.f, 1.f);
    Check(!p.empty() && p.back() == 200 % kPulsesPerCycle, "loop: positions past the cycle wrap");
    clock.ClearLoop();

    // free running: a tap sets the tempo and puts a beat on it
    clock.Tap(90.f);
    Check(clock.GetTempo() == 90, "free tap: the tempo");
    p = Pulses(clock, 0.f, 1.f);
    Check(p.size() == 1 && p[0] % kPulsesPerBeat == 0, "free tap: a beat on the tap");
    clock.Tap(500.f);
    Check(clock.GetTempo() == kMaxBpm, "free tap: clamped to 300");
}

static void TestMidiClock()
{
    MidiClock midi;
    TempoClock clock;
    clock.Init(kSr, &midi);
    midi.has_clock = true;
    midi.bpm = 100.f;
    clock.Process(kBlock);
    Check(clock.GetTempo() == 100 && clock.TempoSet(), "clock: its tempo, and a tempo is set");
    Check(!clock.CanTap(), "clock, no loop: taps refused");
    clock.Tap(150.f);
    Check(clock.GetTempo() == 100, "clock, no loop: a tap changes nothing");

    // a loop overrides the clock, and its taps count
    clock.SetLoop(3 * 48000, 4);
    Pulses(clock, 0.f, 1.f);
    Check(clock.GetTempo() == 80 && clock.CanTap(), "loop over the clock: the loop's tempo, taps taken");
    midi.bpm = 130.f;
    midi.ticks = 48;
    Check(Pulses(clock, 0.f, 1.f).empty(), "loop over the clock: clock ticks give no pulses");
    Check(clock.GetTempo() == 80, "loop over the clock: a clock tempo change doesn't follow");
    clock.ClearLoop();
    clock.Process(kBlock);
    Check(clock.GetTempo() == 130, "erased: the clock again");
    midi.ticks = 52;
    Check(clock.Process(kBlock) == 2, "erased: counting the clock from where it was");
}

/** A 240 BPM clock to a looper: a tick every 500 samples */
static void TestQuantizedBeats()
{
    static int16_t mem[kLoopMemSize];
    MidiClock midi;
    chompi::Looper looper;
    looper.Init(mem, &midi);
    const float period = 500.f;
    midi.has_clock = true;
    midi.tick_period = period;
    midi.bpm = kSr * 60.f / (period * kTicksPerBeat);

    float in[kBlock] = {}, out_l[kBlock], out_r[kBlock];
    size_t now = 0;
    auto block = [&]() {
        now += kBlock;
        midi.ticks = static_cast<uint32_t>(now / period);
        midi.last_tick_time = static_cast<uint32_t>(midi.ticks * period);
        looper.Process(in, in, out_l, out_r, kBlock);
    };

    looper.StartRecording(true);
    block();
    // 1.5 bars (a bar is 96 ticks, 48000 samples), then stop: it records to the end of bar 2
    while (now < 72000)
        block();
    looper.StopRecording();
    while (looper.GetState() == chompi::Looper::State::RECORDING && now < 200000)
        block();
    Check(looper.GetState() == chompi::Looper::State::PLAYING && looper.GetBeats() == 8,
          "quantized: 2 bars, 8 beats");
    Check(looper.GetLength() > 95000 && looper.GetLength() < 97000, "quantized: 2 bars long");

    looper.Erase();
    for (int i = 0; i < 2000; i++)
        block();
    looper.StartRecording(false);
    for (int i = 0; i < 100; i++)
        block();
    looper.StopRecording();
    block();
    Check(looper.GetState() == chompi::Looper::State::PLAYING && looper.GetBeats() == 0,
          "unquantized: no beats known");
}

/** Bar lines: the pulses Pulse() returns that IsBarLine, against PulsesToBarLine's estimate */
static void TestBarLines()
{
    MidiClock midi;
    TempoClock clock;
    clock.Init(kSr, &midi);

    // free: every 48 pulses
    for (int i = 0; i < 10; i++)
        clock.Pulse();
    Check(clock.PulsesToBarLine() == 38 && clock.PulsesPerBarLine() == kPulsesPerBar,
          "bar lines, free: 38 pulses after the 10th");
    uint32_t n = 0;
    while (!TempoClock::IsBarLine(clock.Pulse()))
        n++;
    Check(n + 1 == 38, "bar lines, free: and there it is");
    Check(clock.PulsesToBarLine() == kPulsesPerBar, "bar lines, free: right after one, a whole bar");
    Check(fabsf(clock.PulseSamples() - 2000.f) < .01f, "bar lines: 2000 samples a pulse at 120 BPM");

    // loops of 2, 4 and 6 beats, forward: a bar line every 48 pulses and on every wrap
    const uint32_t beats[] = {2, 4, 6};
    const uint32_t gaps[][2] = {{24, 24}, {48, 48}, {48, 24}};
    for (size_t l = 0; l < 3; l++)
    {
        const uint32_t pulses = beats[l] * kPulsesPerBeat;
        clock.SetLoop(pulses * 1000, beats[l]);
        Pulses(clock, 0.f, 1.f); // beat 1
        bool ok = true;
        uint32_t idx = 0;
        for (size_t gap = 0; gap < 2; gap++)
        {
            ok = ok && clock.PulsesToBarLine() == gaps[l][gap];
            uint32_t count = 0;
            do
            {
                idx++;
                count++;
                Pulses(clock, (idx % pulses + .5f) / pulses, 1.f);
            } while (!TempoClock::IsBarLine(clock.Position()));
            ok = ok && count == gaps[l][gap];
        }
        char what[80];
        snprintf(what, sizeof(what), "bar lines, %u-beat loop: %u then %u pulses apart",
                 static_cast<unsigned>(beats[l]), static_cast<unsigned>(gaps[l][0]),
                 static_cast<unsigned>(gaps[l][1]));
        Check(ok, what);
        Check(clock.PulsesPerBarLine() == (pulses < kPulsesPerBar ? pulses : kPulsesPerBar),
              "bar lines: a loop shorter than a bar has one every pass");
    }

    // reverse, a 4-beat loop crossing back over pulse 10: 9 down to 0 to go
    clock.SetLoop(48000, 4);
    Pulses(clock, 0.f, 1.f);
    Pulses(clock, 10.5f / 48.f, 1.f);
    Pulses(clock, 9.5f / 48.f, -1.f);
    Check(clock.Reverse() && clock.PulsesToBarLine() == 10, "bar lines, reverse: 9 down to 0 is 10 pulses");
    clock.ClearLoop();
}

/** Records what a morph sends, in place of the FxChain */
struct FakeChain
{
    float params[kNumFx][kNumFxParams] = {};
    bool on[kNumFx] = {};
    void SetParam(size_t fx, size_t p, float v) { params[fx][p] = v; }
    void SetOn(size_t fx, bool o) { on[fx] = o; }
    void FastSlew() {}
};

/** One block as the engine runs it: the clock's pulses, then the morph. Returns whether it
 *  had a bar line */
static bool MorphBlock(TempoClock& clock, FxMorphT<FakeChain>& morph)
{
    bool bar = false;
    const uint32_t n = clock.Process(kBlock);
    for (uint32_t i = 0; i < n; i++)
    {
        const bool line = TempoClock::IsBarLine(clock.Pulse());
        bar = bar || line;
        morph.Pulse(line);
    }
    morph.Process(kBlock, clock.PulseSamples());
    return bar;
}

static FxMorphPlan Plan(MorphParam how, float from, float to)
{
    FxMorphPlan plan;
    plan.deferred = plan.wake = plan.was_on = 0;
    for (size_t fx = 0; fx < kNumFx; fx++)
        for (size_t p = 0; p < kNumFxParams; p++)
        {
            plan.start[fx][p] = from;
            plan.target[fx][p] = to;
            plan.how[fx][p] = how;
        }
    return plan;
}

static void TestMorph()
{
    MidiClock midi;
    TempoClock clock;
    clock.Init(kSr, &midi); // free at 120 BPM: a bar is 2s, 4000 blocks
    FakeChain chain;
    FxMorphT<FakeChain> morph;
    morph.Init(&chain);

    // a quarter into a bar, one tap: lands on the bar line, 1.5s on
    while (clock.Position() != 12)
        MorphBlock(clock, morph);
    FxMorphPlan plan = Plan(MorphParam::GLIDE, 0.f, 1.f);
    plan.how[FX_DELAY][0] = MorphParam::HOLD;
    morph.Start(plan, clock.PulsesToBarLine());
    Check(morph.Active() && chain.params[FX_FILTER][0] == 0.f, "morph: starts where it is");
    int blocks = 0;
    float half = -1.f, last = 0.f;
    bool rising = true, held = true;
    bool bar = false;
    while (!bar)
    {
        bar = MorphBlock(clock, morph);
        blocks++;
        if (blocks == 1500)
            half = chain.params[FX_FILTER][0];
        rising = rising && chain.params[FX_FILTER][0] >= last;
        last = chain.params[FX_FILTER][0];
        if (!bar)
            held = held && chain.params[FX_DELAY][0] == 0.f;
    }
    Check(!morph.Active() && chain.params[FX_FILTER][0] == 1.f && chain.params[FX_DELAY][0] == 1.f,
          "morph: landed on the bar line, every parameter on the scene's");
    Check(blocks > 2990 && blocks <= 3001, "morph: three quarters of a bar, 3000 blocks");
    Check(fabsf(half - .5f) < .01f, "morph: halfway there halfway through");
    Check(rising, "morph: a steady glide");
    Check(held, "morph: a held (stepped) parameter switches only at the landing");

    // one tap just before a bar line still lands on it: strict
    while (clock.Position() != 47)
        MorphBlock(clock, morph);
    morph.Start(Plan(MorphParam::GLIDE, 0.f, 1.f), clock.PulsesToBarLine());
    blocks = 0;
    while (!MorphBlock(clock, morph))
        blocks++;
    Check(!morph.Active() && blocks < 100, "morph: a tap in the bar's last pulse lands on its line");

    // two taps, the second a quarter of a bar in: two bar lines, no jump at the tap
    while (clock.Position() != 0)
        MorphBlock(clock, morph);
    morph.Start(Plan(MorphParam::GLIDE, 0.f, 1.f), clock.PulsesToBarLine());
    for (int i = 0; i < 1000; i++)
        MorphBlock(clock, morph);
    const float before = chain.params[FX_FILTER][0];
    Check(morph.AddBar(clock.PulsesPerBarLine()), "morph: a second tap adds a bar");
    MorphBlock(clock, morph);
    Check(fabsf(chain.params[FX_FILTER][0] - before) < .002f, "morph: no jump at the tap");
    int lines = 0;
    blocks = 1001;
    while (morph.Active())
    {
        lines += MorphBlock(clock, morph);
        blocks++;
        if (blocks == 4000)
            half = chain.params[FX_FILTER][0];
    }
    Check(lines == 2 && blocks > 7990 && blocks <= 8001, "morph: two taps land on the second bar line");
    // a quarter there at the tap, then the rest over 7000 blocks: 3000 of them at 4000
    Check(fabsf(half - (.25f + .75f * 3.f / 7.f)) < .01f && chain.params[FX_FILTER][0] == 1.f,
          "morph: and glide over both bars");

    // the most taps
    morph.Start(Plan(MorphParam::GLIDE, 0.f, 1.f), clock.PulsesToBarLine());
    int taps = 1;
    while (morph.AddBar(clock.PulsesPerBarLine()))
        taps++;
    Check(taps == static_cast<int>(kMaxMorphBars), "morph: up to 8 bars");
    morph.Land();
    Check(!morph.Active() && chain.params[FX_FILTER][0] == 1.f, "morph: landed early, on the scene");

    // fade out: the amount glides to its default, the key stays on until the landing;
    // fade in: the key comes on once the amount is down
    plan = Plan(MorphParam::HOLD, .7f, .7f);
    plan.how[FX_DELAY][3] = MorphParam::FADE_OUT;
    plan.start[FX_DELAY][3] = .8f;
    plan.target[FX_DELAY][3] = .3f;
    plan.how[FX_REVERB][3] = MorphParam::GLIDE;
    plan.start[FX_REVERB][3] = 0.f;
    plan.target[FX_REVERB][3] = .9f;
    plan.deferred = (1u << FX_DELAY) | (1u << FX_REVERB);
    plan.wake = 1u << FX_REVERB;
    chain.on[FX_DELAY] = true;
    while (clock.Position() != 0)
        MorphBlock(clock, morph);
    morph.Start(plan, clock.PulsesToBarLine());
    morph.SetOn(FX_DELAY, false);
    morph.SetOn(FX_REVERB, true);
    Check(chain.on[FX_DELAY] && !chain.on[FX_REVERB], "fades: no key switched at the press");
    for (int i = 0; i < 60; i++)
        MorphBlock(clock, morph);
    Check(chain.on[FX_REVERB], "fades: the fade-in's key on after 25ms");
    bool still_on = true;
    float delay_level = 1.f;
    while (morph.Active())
    {
        still_on = still_on && chain.on[FX_DELAY];
        delay_level = chain.params[FX_DELAY][3];
        MorphBlock(clock, morph);
    }
    Check(still_on && delay_level < .01f, "fades: the fade-out's key on, its level down to 0");
    Check(!chain.on[FX_DELAY] && chain.params[FX_DELAY][3] == .3f,
          "fades: at the landing, the key off and the level the scene's");
    Check(chain.params[FX_REVERB][3] == .9f, "fades: the fade-in's level the scene's");

    // a knob turned meanwhile moves the destination
    morph.Start(Plan(MorphParam::GLIDE, 0.f, 1.f), clock.PulsesToBarLine());
    Check(morph.SetParam(FX_FILTER, 0, .4f), "turned meanwhile: goes to the morph");
    while (morph.Active())
        MorphBlock(clock, morph);
    Check(chain.params[FX_FILTER][0] == .4f, "turned meanwhile: lands there");
    Check(!morph.SetParam(FX_FILTER, 0, .2f) && !morph.SetOn(FX_FILTER, true),
          "after it: straight to the chain");

    // stopped halfway (SHIFT + PLAY): everything stays there, the fade-out's key still on
    while (clock.Position() != 0)
        MorphBlock(clock, morph);
    plan = Plan(MorphParam::GLIDE, 0.f, 1.f);
    plan.deferred = plan.was_on = 1u << FX_DELAY;
    chain.on[FX_DELAY] = true;
    morph.Start(plan, clock.PulsesToBarLine());
    morph.SetOn(FX_DELAY, false);
    for (int i = 0; i < 2000; i++)
        MorphBlock(clock, morph);
    float live[kNumFx][kNumFxParams];
    uint16_t unswitched = 0, was_on = 0;
    Check(morph.Freeze(live, &unswitched, &was_on) && !morph.Active(), "stopped: it ends");
    Check(fabsf(live[FX_FILTER][0] - .5f) < .01f && chain.params[FX_FILTER][0] == live[FX_FILTER][0],
          "stopped: halfway, and that's what the chain has");
    Check(unswitched == (1u << FX_DELAY) && was_on == (1u << FX_DELAY) && chain.on[FX_DELAY],
          "stopped: the key it hadn't switched stays as it was");
    for (int i = 0; i < 4000; i++)
        MorphBlock(clock, morph);
    Check(chain.params[FX_FILTER][0] == live[FX_FILTER][0] && chain.on[FX_DELAY],
          "stopped: no landing at the bar line");
    Check(!morph.Freeze(live, &unswitched, &was_on), "stopped: nothing to stop any more");
}

int main()
{
    TestTaps();
    TestFit();
    TestLoopClock();
    TestMidiClock();
    TestQuantizedBeats();
    TestBarLines();
    TestMorph();
    if (failures)
        printf("%d failed\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
