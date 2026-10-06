// tempo.cpp: checks the FX's tempo on the host: tap tempo (TapTempo.h), fitting and guessing a
// loop's beats, the tempo clock locked to a loop (TempoClock.h) and a quantized loop's beats
// (Looper.h), with a faked MIDI clock where it matters. Exits 0 when everything passes. Run
// by tempo.sh.
#include <cmath>
#include <cstdio>
#include <vector>
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
    Looper looper;
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
    while (looper.GetState() == Looper::State::RECORDING && now < 200000)
        block();
    Check(looper.GetState() == Looper::State::PLAYING && looper.GetBeats() == 8,
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
    Check(looper.GetState() == Looper::State::PLAYING && looper.GetBeats() == 0,
          "unquantized: no beats known");
}

int main()
{
    TestTaps();
    TestFit();
    TestLoopClock();
    TestMidiClock();
    TestQuantizedBeats();
    if (failures)
        printf("%d failed\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
