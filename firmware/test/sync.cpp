// sync.cpp: FRIZZ's timing against a MIDI clock on the virtual CHOMPI, measured: how steady the
// FX's tempo stays under a real sender's jitter (a hardware sequencer on the jack, a DAW over
// USB), how close a quantized loop comes to the bars it should hold and how far it drifts from
// the clock afterwards (nothing pulls it back), what happens when the clock stops, changes or
// switches during a recording, the 2:45 limit, and tap tempo from the panel. Each case on a
// fresh device, its numbers printed under it. Run by unit.sh sync.
#include "timing.h"
#include "twin.h"

// what counts as right
static const double kMaxLoopError = 24.;  // samples: a quantized loop within a block of its bars
static const double kMaxDriftPerMin = 1.; // ms a minute against the clock, once it loops
static const double kMaxRampLagMs = 500.; // the FX at a ramp's end tempo this soon after it

// the senders: exact; a hardware sequencer (its timer's jitter, its crystal 50 ppm slow); a DAW
// over USB (1 ms frames, its scheduling's jitter); a host that sends its ticks in pairs, so two
// arrive in one block; one that catches up, every other tick late, a frame before the next
struct Sender
{
    const char* name;
    double jitter_ms;
    bool usb;
    double drift_ppm;
    bool pairs;
    double pair_gap_ms;
};
static const Sender kExact = {"exact", 0., false, 0., false, 0.};
static const Sender kSequencer = {"sequencer", .2, false, 50., false, 0.};
static const Sender kDaw = {"DAW", .3, true, 0., false, 0.};
static const Sender kPairs = {"pairs", .3, true, 0., true, 0.};
static const Sender kCatchUp = {"catch-up", .3, true, 0., true, 1.};
static const Sender* const kSenders[] = {&kExact, &kSequencer, &kDaw, &kPairs, &kCatchUp};

static void StartClock(const Sender& s, double bpm)
{
    const ClockGen::Config c = Clock(bpm, s.jitter_ms, s.usb, s.drift_ppm, s.pairs, s.pair_gap_ms);
    if (s.usb)
        StartUsb(c);
    else
        StartTrs(c);
}
static ClockGen& Gen(const Sender& s) { return s.usb ? usb : trs; }

static std::string Name(const char* what, const Sender& s, double bpm)
{
    char b[64];
    snprintf(b, sizeof(b), "%s, %s at %g BPM: ", what, s.name, bpm);
    return b;
}

/** The FX grid's pulses: the blocks the position Probe() gives moved in. A pulse comes every
 *  16 ms or more at 300 BPM, so never two in a block (0.5 ms), and the position wraps at the
 *  4-bar cycle or a shorter loop's end, so it's the move that counts, not by how much */
struct PulseCounter
{
    uint32_t last = 0;
    uint64_t count = 0;
    void Reset()
    {
        last = Probe().position;
        count = 0;
    }
    void Step()
    {
        const uint32_t now = Probe().position;
        count += now != last;
        last = now;
    }
};

/** The loop's wraps, block by block: how far they move from its ideal length */
struct WrapWatch
{
    std::vector<double> wraps;
    float last = 0.f;
    void Reset()
    {
        wraps.clear();
        last = Probe().loop_pos;
    }
    void Step()
    {
        const float pos = Probe().loop_pos;
        if (pos + .5f < last)
            wraps.push_back(BlockMs());
        last = pos;
    }
    /** The largest drift against ideal_ms a pass, and its rate in ms a minute */
    void Drift(double ideal_ms, double& max_ms, double& per_min) const
    {
        max_ms = per_min = 0.;
        for (size_t k = 1; k < wraps.size(); k++)
        {
            const double d = wraps[k] - wraps[0] - k * ideal_ms;
            if (fabs(d) > fabs(max_ms))
                max_ms = d;
        }
        if (wraps.size() > 1)
        {
            const size_t k = wraps.size() - 1;
            per_min = (wraps[k] - wraps[0] - k * ideal_ms) / (wraps[k] - wraps[0]) * 60000.;
        }
    }
};

// ======== the FX's tempo from a running clock, no loop ========

static void TempoCase(const Sender& s, double bpm)
{
    StartClock(s, bpm);
    RunMs(kReadyMs);
    RunMs(3000); // settled
    const int want = static_cast<int>(Gen(s).Bpm(BlockMs()) + .5);
    int changes = 0, lo = 1000, hi = 0, last = Probe().tempo;
    float bpm_lo = 1000.f, bpm_hi = 0.f;
    PulseCounter pulses;
    pulses.Reset();
    const uint32_t ticks0 = Probe().ticks;
    each_block = [&] {
        const ClockState c = Probe();
        changes += c.tempo != last;
        last = c.tempo;
        lo = std::min(lo, c.tempo);
        hi = std::max(hi, c.tempo);
        bpm_lo = std::min(bpm_lo, c.midi_bpm);
        bpm_hi = std::max(bpm_hi, c.midi_bpm);
        pulses.Step();
    };
    RunMs(30000);
    each_block = nullptr;
    const uint32_t ticks = Probe().ticks - ticks0;
    Report("%-9s %5.1f BPM: FX tempo %d..%d, %d changes in 30 s; clock read %.2f..%.2f; "
           "%llu pulses for %u ticks",
           s.name, bpm, lo, hi, changes, bpm_lo, bpm_hi, (unsigned long long)pulses.count, ticks);
    const bool whole = bpm == floor(bpm);
    Record(!whole ? "tempo-between" : bpm <= 120. ? "tempo-to-120" : "tempo-above-120",
           changes == 0 && lo == want && hi == want ? 0. : changes + 1);
    const long diff = static_cast<long>(pulses.count) - static_cast<long>(ticks / 2);
    Check(diff >= -1 && diff <= 1, (Name("tempo", s, bpm) + "a pulse every 2 ticks").c_str());
}

// ======== quantized loops from the panel ========

/** PLAY held, LOOP: a quantized recording; returns its start (the block it began in) */
static double StartQuantized()
{
    Press("KEY_27", true);
    RunMs(80);
    Press("KEY_28", true);
    RunUntil([] { return Probe().loop_state == 1; }, 200);
    const double start = BlockMs();
    RunMs(60);
    Press("KEY_28", false);
    RunMs(20);
    Press("KEY_27", false);
    return start;
}

/** LOOP, so that the firmware takes it at_ms after the recording's start */
static void StopAt(double start, double at_ms)
{
    // a key counts as pressed ~7.5 ms after it goes down (its debouncing): press that early
    while (BlockMs() < start + at_ms - 7.5)
        RunBlocks(1);
    Tap("KEY_28");
}

/** factor: the settings page's clock factor (SettingsPage.h), 2 or .5 counting the clock's
 *  ticks twice or every other one, so its bars are half or twice as long */
static void LoopCase(const Sender& s, double bpm, int bars, double factor = 1.)
{
    StartClock(s, bpm);
    RunMs(kReadyMs);
    if (factor != 1.)
    {
        SetToggle(true);
        RunMs(200);
        Tap("KEY_12"); // the factor's key: x1 to x2, again to x1/2
        if (factor < 1.)
        {
            RunMs(200);
            Tap("KEY_12");
        }
        SetToggle(false);
        RunMs(1000);
    }
    const double tick = Gen(s).TickSamples(BlockMs()) / factor;
    const double bar_ms = 96. * tick / 48.;
    StopAt(StartQuantized(), (bars - .5) * bar_ms);
    RunUntil([] { return Probe().loop_state == 2; }, bar_ms + 500.);
    const ClockState c = Probe();
    const double ideal = bars * 96. * tick;
    const double err = static_cast<double>(c.loop_length) - ideal;

    PulseCounter pulses;
    WrapWatch watch;
    pulses.Reset();
    watch.Reset();
    each_block = [&] {
        pulses.Step();
        watch.Step();
    };
    RunMs(30000);
    each_block = nullptr;
    double max_drift, measured;
    watch.Drift(ideal / 48., max_drift, measured);
    // the drift a minute from the length's error: the wraps measure it only to a block (0.5 ms),
    // too coarse over 30 s for a limit of 1 ms a minute, so they only have to agree with it
    const double per_min = err / 48. * (60000. / (ideal / 48.));
    const double want_pulses = 30000. / (ideal / 48.) * c.loop_beats * 12.;
    Report("%-9s %3.0f BPM, %d bar%s: %zu samples for %.1f (%+.1f), %u beats; drift %+.2f ms/min "
           "(%+.1f ms after 100 passes), measured %+.2f ms in 30 s; %llu pulses for %.0f",
           s.name, bpm, bars, bars > 1 ? "s" : " ", c.loop_length, ideal, err, c.loop_beats,
           per_min, err * 100. / 48., max_drift, (unsigned long long)pulses.count, want_pulses);
    std::string base = Name("loop", s, bpm) + std::to_string(bars) + (bars > 1 ? " bars, " : " bar, ");
    if (factor != 1.)
        base += factor > 1. ? "x2, " : "x1/2, ";
    Check(c.loop_state == 2 && c.loop_beats == static_cast<uint32_t>(bars * 4),
          (base + "closes on its bars").c_str());
    Check(fabs(pulses.count - want_pulses) <= 2., (base + "12 pulses a beat of the loop").c_str());
    // the last wrap against where the length error puts it after as many passes; each wrap is
    // seen to a block, so the two agree within two blocks
    const size_t passes = watch.wraps.size() > 1 ? watch.wraps.size() - 1 : 0;
    const double seen = passes ? watch.wraps[passes] - watch.wraps[0] - passes * ideal / 48. : 0.;
    Check(passes > 0 && fabs(seen - passes * err / 48.) <= 1.,
          (base + "its loop point moves as its length says").c_str());
    if (factor != 1.)
        return; // the summary's figures are the clock as sent
    Record(&s == &kExact ? "loop-error-exact" : &s == &kCatchUp ? "loop-error-catch-up" : "loop-error-real", err);
    Record("loop-drift", per_min);
}

/** A free loop: LOOP, LOOP after ms */
static void RecordFree(double ms)
{
    Tap("KEY_28");
    RunMs(ms - 60);
    Tap("KEY_28");
    RunMs(600); // LOOP doesn't erase for 500 ms after a stop
}

/** SHIFT (CHOMPI held) + LOOP n times, every ms */
static void TapTempo(int n, double ms)
{
    Press("KEY_26", true);
    RunMs(100);
    for (int i = 0; i < n; i++)
    {
        Tap("KEY_28");
        RunMs(ms - 60);
    }
    Press("KEY_26", false);
    RunMs(300);
}

int main()
{
    // the FX's tempo from a clock, no loop
    for (const Sender* s : kSenders)
        for (double bpm : {60., 90., 120., 174., 300.})
            cases.push_back({"tempo", [s, bpm] { TempoCase(*s, bpm); }});

    // a tempo between two whole BPM: it rounds to one, and stays there
    for (const Sender* s : kSenders)
        cases.push_back({"tempo-between", [s] { TempoCase(*s, 120.4); }});

    // a ramp over USB: how far behind the FX's tempo stays
    cases.push_back({"ramp", [] {
        RunMs(kReadyMs);
        StartUsb(Clock(100., .3));
        RunMs(3000);
        ClockGen::Config c = Clock(100., .3);
        c.ramp_to = 140.;
        c.ramp_ms = 8000.;
        StartUsb(c);
        const double t0 = BlockMs();
        double worst = 0.;
        each_block = [&] { worst = std::max(worst, fabs(Probe().tempo - usb.Bpm(BlockMs()))); };
        RunMs(8000);
        each_block = nullptr;
        RunUntil([] { return Probe().tempo == 140; }, 3000);
        const double lag = BlockMs() - t0 - 8000.;
        Report("ramp 100 -> 140 BPM in 8 s over USB: at most %.1f BPM behind, at 140 %.0f ms "
               "after the ramp's end", worst, lag);
        Check(lag < kMaxRampLagMs, "ramp: the FX reach the ramp's tempo within 0.5 s of its end");
    }});

    // quantized loops: their length, the drift, the FX grid on them
    for (const Sender* s : kSenders)
        for (double bpm : {90., 120., 174.})
            for (int bars : {1, 4})
                cases.push_back({"loop", [s, bpm, bars] { LoopCase(*s, bpm, bars); }});

    // the same at the clock factor's x2 and x1/2: a pair, or a late tick catching up, counts
    // four ticks at one time at x2
    for (const Sender* s : {&kExact, &kPairs, &kCatchUp})
        for (double factor : {2., .5})
            cases.push_back({"loop-factor", [s, factor] { LoopCase(*s, 120., 1, factor); }});

    // the strict rule: a stop just after a bar line records another bar, just before doesn't
    for (double off : {-12., 12.})
        cases.push_back({"bar-line", [off] {
            StartTrs(Clock(120.));
            RunMs(kReadyMs);
            StopAt(StartQuantized(), 2 * 2000. + off);
            RunUntil([] { return Probe().loop_state == 2; }, 3000);
            const ClockState c = Probe();
            Report("LOOP %+.0f ms from bar 2's end: %u beats, %zu samples", off, c.loop_beats,
                   c.loop_length);
            Check(c.loop_beats == (off < 0 ? 8u : 12u),
                  off < 0 ? "bar line: a stop 12 ms before it closes there"
                          : "bar line: a stop 12 ms after it records another bar");
        }});

    // the clock stops mid-recording: it closes where it is, as an unquantized loop (LOOPER.md 1.3)
    cases.push_back({"clock-lost", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        const double start = StartQuantized();
        while (BlockMs() < start + 3000.)
            RunBlocks(1);
        trs.Stop();
        const bool closed = RunUntil([] { return Probe().loop_state == 2; }, 1500);
        const double closed_at = BlockMs() - start;
        const ClockState c = Probe();
        Report("clock stopped 3 s in: closed after %.0f ms, %zu samples (%.0f ms), %u beats, FX at %d",
               closed_at, c.loop_length, c.loop_length / 48., c.loop_beats, c.tempo);
        Check(closed && c.loop_beats == 0, "clock lost: the recording closes, unquantized");
        Check(fabs(c.loop_length / 48. - closed_at) < 5.,
              "clock lost: it keeps everything up to where it closed");
    }});

    // the jack's clock stops and USB's takes over mid-recording: a new lock, so it closes too
    cases.push_back({"source-switch", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        const double start = StartQuantized();
        while (BlockMs() < start + 2500.)
            RunBlocks(1);
        trs.Stop();
        StartUsb(Clock(120.));
        const bool closed = RunUntil([] { return Probe().loop_state == 2; }, 1500);
        const ClockState c = Probe();
        Report("jack -> USB 2.5 s in: closed after %.0f ms, %zu samples, %u beats, source %d",
               BlockMs() - start, c.loop_length, c.loop_beats, c.source);
        Check(closed && c.loop_beats == 0, "source switch: the recording closes, unquantized");
    }});

    // the clock changes tempo mid-recording: what the loop becomes (no rule for it: reported)
    cases.push_back({"tempo-change", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        const double start = StartQuantized();
        while (BlockMs() < start + 2000.)
            RunBlocks(1);
        StartTrs(Clock(100.)); // a bar at 120, then on at 100
        StopAt(start, 2000. + 1.5 * 2400.);
        RunUntil([] { return Probe().loop_state == 2; }, 4000);
        const ClockState c = Probe();
        // what the DAW played from the press to the bar the stop was in: 1 bar at 120, 2 at 100
        Report("120 -> 100 BPM after a bar, stopped in bar 3: %zu samples (%.0f ms), %u beats; "
               "the DAW's 3 bars were %.0f ms, the loop's FX tempo %.2f",
               c.loop_length, c.loop_length / 48., c.loop_beats, 2000. + 2. * 2400., c.fx_bpm);
        Check(c.loop_state == 2, "tempo change: the recording closes");
    }});

    // after closing, the loop is the clock: a tempo change doesn't follow, an erase does
    cases.push_back({"after-close", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        StopAt(StartQuantized(), 1000.);
        RunUntil([] { return Probe().loop_state == 2; }, 2000);
        const size_t length = Probe().loop_length;
        StartTrs(Clock(140.));
        RunMs(3000);
        Check(Probe().tempo == 120 && Probe().loop_length == length,
              "after close: the clock's new tempo doesn't change the loop or the FX's tempo");
        Tap("KEY_28"); // erase
        RunUntil([] { return Probe().loop_state == 0; }, 500);
        const bool follows = RunUntil([] { return Probe().tempo == 140; }, 500);
        PulseCounter pulses;
        pulses.Reset();
        const uint32_t ticks0 = Probe().ticks;
        each_block = [&] { pulses.Step(); };
        RunMs(5000);
        each_block = nullptr;
        const uint32_t ticks = Probe().ticks - ticks0;
        Report("erased: FX at %d, %llu pulses for %u ticks", Probe().tempo,
               (unsigned long long)pulses.count, ticks);
        Check(follows, "after close: erased, the FX follow the clock again");
        Check(llabs((long long)pulses.count - ticks / 2) <= 1,
              "after close: erased, a pulse every 2 ticks again");
    }});

    // a song change behind a loop: whatever the clock does once the loop is closed (a tempo
    // just off, another tempo, stopped, back over USB, a second clock on the jack), the loop and
    // the FX keep the tempo it was recorded at, block for block, and the loop point its pace
    cases.push_back({"song-change", [] {
        StartUsb(Clock(120., .3));
        RunMs(kReadyMs);
        StopAt(StartQuantized(), 3000.);
        RunUntil([] { return Probe().loop_state == 2; }, 3000);
        const ClockState at_close = Probe();
        int moved = 0;
        WrapWatch watch;
        watch.Reset();
        each_block = [&] {
            const ClockState c = Probe();
            moved += c.tempo != at_close.tempo || c.fx_bpm != at_close.fx_bpm
                     || c.loop_length != at_close.loop_length || c.loop_beats != at_close.loop_beats
                     || c.loop_state != 2;
            watch.Step();
        };
        StartUsb(Clock(120.3, .3)); // the next song, nearly the same tempo
        RunMs(8000);
        StartUsb(Clock(98., .3)); // another
        RunMs(8000);
        usb.Stop(); // stopped
        RunMs(3000);
        StartUsb(Clock(140., .3)); // started again, faster
        RunMs(6000);
        StartTrs(Clock(87.)); // and a sequencer on the jack as well
        RunMs(6000);
        usb.Stop(); // the jack's takes over
        RunMs(6000);
        each_block = nullptr;
        double worst = 0.;
        for (size_t k = 1; k < watch.wraps.size(); k++)
            worst = std::max(worst, fabs(watch.wraps[k] - watch.wraps[k - 1]
                                         - at_close.loop_length / 48.));
        Report("song change behind a 2-bar loop: FX at %d (%.3f BPM), %zu samples; %d blocks of 37 s "
               "changed anything; %zu passes, each within %.2f ms of the loop's length",
               at_close.tempo, at_close.fx_bpm, at_close.loop_length, moved,
               watch.wraps.size() ? watch.wraps.size() - 1 : 0, worst);
        Check(moved == 0,
              "song change: the clock's tempo, stop, restart and source don't touch the loop or the FX's tempo");
        Check(watch.wraps.size() >= 9 && worst <= .5,
              "song change: and the loop keeps its pace, every pass its own length");
    }});

    // the loop point of a quantized loop: its end now follows the ticks up to the close, and
    // can land past the recording's write position by up to a block or more; the post-roll's
    // crossfade must still take it over without a click (a sine through it, and the largest
    // step between two samples against the sine's own)
    for (const Sender* s : kSenders)
        for (double bpm : {120., 174.})
            cases.push_back({"loop-point", [s, bpm] {
                sine_amp = .3f;
                StartClock(*s, bpm);
                RunMs(kReadyMs);
                max_step = 0.f;
                RunMs(1000);
                const float sine_step = max_step;
                const double bar_ms = 96. * Gen(*s).TickSamples(BlockMs()) / 48.;
                StopAt(StartQuantized(), .5 * bar_ms);
                RunUntil([] { return Probe().loop_state == 2; }, bar_ms + 500.);
                RunMs(50); // the mix's jump to the loop
                max_step = 0.f;
                WrapWatch watch;
                watch.Reset();
                each_block = [&] { watch.Step(); };
                RunMs(8000);
                each_block = nullptr;
                Report("%-9s %3.0f BPM, 1 bar: largest step %.4f over %zu loop points, the sine's %.4f",
                       s->name, bpm, max_step, watch.wraps.size(), sine_step);
                Check(watch.wraps.size() >= 3 && max_step <= 1.5f * sine_step,
                      (Name("loop point", *s, bpm) + "no click where the loop wraps").c_str());
            }});

    // the 2:45 limit, quantized: cut back to the last whole bar (LOOPER.md 1.3a)
    cases.push_back({"limit", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        StartQuantized();
        RunUntil([] { return Probe().loop_state == 2; }, 170000);
        const ClockState c = Probe();
        Report("2:45 at 120 BPM: %zu samples (%.3f s), %u beats", c.loop_length,
               c.loop_length / 48000., c.loop_beats);
        Check(c.loop_beats == 82 * 4 && llabs((long long)c.loop_length - 82 * 96000) <= 24,
              "limit: a quantized recording at 2:45 is cut back to 82 whole bars");
    }});

    // tap tempo from the panel (SHIFT + LOOP)
    cases.push_back({"tap", [] {
        RunMs(kReadyMs);
        TapTempo(4, 600.);
        Report("4 taps 600 ms apart, no clock: FX at %d", Probe().tempo);
        Check(Probe().tempo == 100, "tap: without a clock, 4 taps set the tempo");
    }});
    cases.push_back({"tap-clock", [] {
        StartTrs(Clock(120.));
        RunMs(kReadyMs);
        TapTempo(4, 600.);
        Report("4 taps 600 ms apart, clock at 120: FX at %d", Probe().tempo);
        Check(Probe().tempo == 120, "tap: with a clock and no loop, taps are refused");
    }});
    cases.push_back({"tap-loop", [] {
        RunMs(kReadyMs);
        RecordFree(2500.);
        const int guessed = Probe().tempo;
        TapTempo(4, 500.);
        Report("a free 2.5 s loop: guessed %d BPM, after taps at 120 BPM: %d", guessed, Probe().tempo);
        Check(guessed == 96, "tap: a free 2.5 s loop with no tempo set is guessed at 4 beats, 96 BPM");
        Check(Probe().tempo == 120, "tap: taps at 120 BPM refit it to 5 beats, 120 BPM");
    }});

    return RunCases([](const std::map<std::string, std::vector<double>>& r) {
        Report("FX tempo changes in 30 s, worst: %.0f up to 120 BPM, %.0f above, %.0f at 120.4",
               std::max(0., Worst(r, "tempo-to-120") - 1), std::max(0., Worst(r, "tempo-above-120") - 1),
               std::max(0., Worst(r, "tempo-between") - 1));
        Check(Worst(r, "tempo-to-120") == 0.,
              "tempo: a clock at whole BPM up to 120, from every sender: the FX's tempo holds still");
        Known(Worst(r, "tempo-above-120") == 0.,
              "tempo: a clock at 174 or 300 BPM, from every sender: the FX's tempo holds still (#19)");
        Known(Worst(r, "tempo-between") == 0.,
              "tempo: a clock between two whole BPM (120.4), from every sender: the FX's tempo holds still (#19)");
        Report("loop length error, worst: %.1f samples from an exact clock, %.1f from a sequencer or "
               "DAW, %.1f from a host catching up; drift, worst: %.2f ms a minute",
               Worst(r, "loop-error-exact"), Worst(r, "loop-error-real"), Worst(r, "loop-error-catch-up"),
               Worst(r, "loop-drift"));
        Check(Worst(r, "loop-error-exact") <= kMaxLoopError,
              "loop: from an exact clock, every quantized loop within a block of its bars");
        Check(Worst(r, "loop-error-real") <= kMaxLoopError,
              "loop: from a sequencer or a DAW, every quantized loop within a block of its bars");
        // #17, found 2026-10-09: every other tick a frame early throws the looper's fit (Looper.h,
        // TrackRecordingClock) off by up to 56 samples on a 1-bar loop, on main as well
        Known(Worst(r, "loop-error-catch-up") <= kMaxLoopError,
              "loop: from a host catching up late ticks, every quantized loop within a block of its bars (#17)");
        Known(Worst(r, "loop-drift") <= kMaxDriftPerMin,
              "loop: every quantized loop drifts under 1 ms a minute against the clock (#20, not planned)");
    });
}
