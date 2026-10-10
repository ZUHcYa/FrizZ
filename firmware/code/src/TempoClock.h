/** @file TempoClock.h
 *  @brief The tempo and clock pulses for what follows it: the delay, the filter LFO, the
 *  freezer, the slicer, the tape stop and the scene morph. Three sources, in this order:
 *
 *  1. A loop (SetLoop): while one exists, it is the clock, whether MIDI clock runs or not.
 *     The loop holds a whole number of beats, and the pulses come from its play position, so
 *     beat 1 is the loop's start and the grid follows the transport: faster, slower,
 *     reversed (the positions then count down). While the loop is paused, the grid runs on
 *     by itself at that tempo, so the FX keep working on the live input; scrubbing doesn't
 *     move it, and on resume it snaps back to the loop's position at the next pulse. The
 *     tempo is the loop's times its speed. A quantized loop knows its beats; an
 *     unquantized one fits the tempo set before it (FitBeats) or, if none was, a guess at
 *     80-160 BPM (GuessBeats). A tap (Tap) refits the beats.
 *  2. MIDI clock: the tempo follows MidiClock rounded to whole BPM (so the delay time
 *     doesn't wobble with the clock's jitter), and pulses are counted from the incoming
 *     ticks, one pulse per 2 ticks (12 PPQN, what TEMPO's delay counts in). The whole BPM
 *     comes from the ticks counted over the last 2-4 s (FollowClock), which holds still under
 *     jitter that MidiClock's smoothed period doesn't (at 174 and 300 BPM, between two whole
 *     BPM), and moves only once the count is past the half by kTempoMargin; a jump in the
 *     tempo is followed at once.
 *  3. Neither: the last tempo is kept (120 BPM until one is set by a clock, a loop or taps)
 *     and pulses come from an internal phase at that tempo. A tap sets the tempo and puts a
 *     beat on the tap.
 *
 *  Pulse() counts the pulses into one position, so every effect that follows the clock
 *  shares one grid: the 16ths, the delay's 8th-note edges (where it rolls its random events)
 *  and the bars are all counted from the same pulse. Without a loop, the count runs from
 *  power-on, or continues across a new MIDI clock lock; MIDI Start and Song Position aren't
 *  read.
 *
 *  Runs in the audio callback, once per block.
 */
#pragma once
#include <math.h>
#include "daisy.h"
#include "MidiClock.h"

namespace chompi
{

static const int kDefaultBpm = 120;
// 2 bars plus the delay's stereo offset must fit in its 10s buffer, which needs > 48 BPM
static const int kMinBpm = 50;
static const int kMaxBpm = 300;
static const uint32_t kPulsesPerBeat = 12;
static const uint32_t kTicksPerPulse = kTicksPerBeat / kPulsesPerBeat; // 24 PPQN MIDI ticks -> 12 PPQN pulses
static const uint32_t kPulsesPer16th = kPulsesPerBeat / 4;
static const uint32_t kPulsesPerEdge = kPulsesPerBeat / 2; // 8th notes
static const uint32_t kPulsesPerBar = kPulsesPerBeat * kBeatsPerBar;
// The pulse position wraps every 4 bars, a multiple of every grid the effects use
static const uint32_t kPulsesPerCycle = 4 * kPulsesPerBar;
// An unquantized loop with no tempo set before it is guessed into this range
static const float kGuessMinBpm = 80.f;
// From a clock: the whole BPM moves once the counted tempo is this far past the half
static const float kTempoMargin = .1f;
// From a clock: the ticks are counted over this long at least, twice this at most, in samples
static const uint32_t kTempoSpan = 2 * 48000;
// From a clock: before the count is this long, the smoothed period's tempo stands in for it
static const uint32_t kTempoMinSpan = 48000;

inline float ClampBpm(float bpm)
{
    return bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);
}

/** A loop of length frames, n beats long: its tempo */
inline float LoopBpm(size_t length, float sample_rate, uint32_t beats)
{
    return 60.f * beats * sample_rate / static_cast<float>(length);
}

/** The beats to give a loop nothing else knows the tempo of: 1, 2, 4, 8 ..., the first that
 *  puts it at kGuessMinBpm or above, so 80-160 BPM (faster only for loops under 0.75s) */
inline uint32_t GuessBeats(size_t length, float sample_rate)
{
    uint32_t beats = 1;
    while (LoopBpm(length, sample_rate, beats) < kGuessMinBpm)
        beats *= 2;
    return beats;
}

/** The whole number of beats that puts a loop closest to bpm, at least 1 */
inline uint32_t FitBeats(size_t length, float sample_rate, float bpm)
{
    const float beats = static_cast<float>(length) * bpm / (60.f * sample_rate);
    return beats < 1.5f ? 1 : static_cast<uint32_t>(beats + .5f);
}

class TempoClock
{
public:
    void Init(float sample_rate, MidiClock* midi_clock)
    {
        sample_rate_ = sample_rate;
        midi_clock_ = midi_clock;
        bpm_ = kDefaultBpm;
        tempo_ = kDefaultBpm;
        tempo_set_ = false;
        phase_ = 0.f;
        had_clock_ = false;
        last_ticks_ = 0;
        pulse_count_ = 0;
        loop_length_ = 0;
        dir_ = 1;
    }

    /** A loop closed: it's the clock from now on, length frames holding beats beats */
    void SetLoop(size_t length, uint32_t beats)
    {
        loop_length_ = length;
        SetLoopBeats(beats);
        // one step before the start, so the first block pulses beat 1
        loop_idx_ = loop_pulses_ - 1;
        paused_ = false;
    }

    /** The loop was erased: back to the MIDI clock, or to free running at the loop's tempo */
    void ClearLoop()
    {
        // the free clock runs at the whole BPM the FX use, so their times stay on its grid
        SetFreeTempo(loop_bpm_);
        loop_length_ = 0;
        paused_ = false;
        had_clock_ = false; // a running clock locks again, from here
        counting_ = false;
        phase_ = 0.f;
        dir_ = 1;
    }

    inline bool HasLoop() const { return loop_length_ > 0; }
    /** Whether a tempo was set (by a clock, a loop or taps), for fitting a new loop to */
    inline bool TempoSet() const { return tempo_set_; }
    /** The tempo without a loop: the clock's or the free one, for fitting a new loop to */
    inline float GetBpm() const { return bpm_; }
    /** Whether a tap would be taken: with a loop always, otherwise only without MIDI clock */
    inline bool CanTap() const { return HasLoop() || !midi_clock_->HasClock(); }

    /** A tapped tempo: refits a loop's beats, or sets the free tempo with a beat now */
    void Tap(float bpm)
    {
        if (bpm <= 0.f || !CanTap())
            return;
        if (HasLoop())
        {
            tempo_set_ = true;
            SetLoopBeats(FitBeats(loop_length_, sample_rate_, bpm));
            loop_idx_ = free_idx_ = PulseIndex(loop_pos_);
            return;
        }
        SetFreeTempo(bpm);
        // the next pulse lands now and starts the nearest beat
        const uint32_t beat = (pulse_count_ + kPulsesPerBeat / 2) / kPulsesPerBeat * kPulsesPerBeat;
        pulse_count_ = (beat + kPulsesPerCycle - 1) % kPulsesPerCycle;
        phase_ = 1.f;
    }

    /** Call once per block, after the looper, with its position (0-1), actual speed and
     *  whether it's paused, when there's a loop. Returns how many pulses happened during it
     *  (usually 0 or 1). */
    uint32_t Process(size_t size, float loop_pos = 0.f, float loop_speed = 1.f,
                     bool loop_paused = false)
    {
        if (HasLoop())
            return ProcessLoop(size, loop_pos, loop_speed, loop_paused);

        uint32_t pulses = 0;
        const bool has_clock = midi_clock_->HasClock();

        if (has_clock)
        {
            // 0 between the first and second tick of a new lock: keep the last tempo
            const float bpm = midi_clock_->GetBpm();
            if (bpm > 0.f)
                FollowClock(bpm);

            const uint32_t ticks = midi_clock_->GetTicks();
            if (!had_clock_)
                last_ticks_ = ticks; // new lock: count from here
            pulses = (ticks - last_ticks_) / kTicksPerPulse;
            last_ticks_ += pulses * kTicksPerPulse;
            phase_ = 0.f;
        }
        else
        {
            counting_ = false;
            pulses = FreePulses(size, bpm_);
        }

        had_clock_ = has_clock;
        return pulses;
    }

    /** Count one pulse; returns the new position, 0..kPulsesPerCycle - 1 */
    uint32_t Pulse()
    {
        if (HasLoop() && paused_)
        {
            free_idx_ = (free_idx_ + 1) % loop_pulses_;
            loop_pulse_ = free_idx_;
            pulse_count_ = free_idx_ % kPulsesPerCycle;
            return pulse_count_;
        }
        if (HasLoop())
        {
            // pulse b sits where the position crosses b / loop_pulses_: forward, that's the
            // index entered; in reverse, the one left
            uint32_t crossed;
            if (dir_ > 0)
                crossed = loop_idx_ = (loop_idx_ + 1) % loop_pulses_;
            else
            {
                crossed = loop_idx_;
                loop_idx_ = (loop_idx_ + loop_pulses_ - 1) % loop_pulses_;
            }
            loop_pulse_ = crossed;
            pulse_count_ = crossed % kPulsesPerCycle;
            return pulse_count_;
        }
        pulse_count_ = (pulse_count_ + 1) % kPulsesPerCycle;
        return pulse_count_;
    }

    /** The pulses from the last one to the next bar line, for a scene morph (FxMorph.h): a
     *  multiple of kPulsesPerBar, or a loop's wrap. An estimate: the transport may change
     *  direction, pause or resume on the way */
    uint32_t PulsesToBarLine() const
    {
        if (!HasLoop())
            return kPulsesPerBar - pulse_count_ % kPulsesPerBar;
        if (!paused_ && dir_ < 0)
            return loop_idx_ % kPulsesPerBar + 1; // the next pulse crosses loop_idx_
        const uint32_t idx = paused_ ? free_idx_ : loop_idx_;
        const uint32_t to_bar = kPulsesPerBar - idx % kPulsesPerBar;
        const uint32_t to_wrap = loop_pulses_ - idx;
        return to_bar < to_wrap ? to_bar : to_wrap;
    }
    /** The pulses between two bar lines: a bar, or a loop shorter than one */
    inline uint32_t PulsesPerBarLine() const
    {
        return HasLoop() && loop_pulses_ < kPulsesPerBar ? loop_pulses_ : kPulsesPerBar;
    }
    /** Whether a position Pulse() returned is on a bar line: every loop wrap is one too */
    static inline bool IsBarLine(uint32_t pos) { return pos % kPulsesPerBar == 0; }
    /** The time between two pulses, in samples: at the loop's real rate while one plays (it
     *  can be slower than kMinBpm, which only limits the FX's tempo), else the tempo's */
    inline float PulseSamples() const
    {
        const float bpm = HasLoop() ? pulse_bpm_ : static_cast<float>(tempo_);
        return bpm > 0.f ? 60.f * sample_rate_ / (bpm * kPulsesPerBeat) : 1e9f;
    }
    /** The position the last Pulse() returned */
    inline uint32_t Position() const { return pulse_count_; }
    /** With a loop: the last Pulse()'s place in it, 0 at its start, up to its pulses - 1:
     *  where a pulse that's a multiple of 3 is a 16th, for MIDI out's Song Position */
    inline uint32_t LoopPulse() const { return loop_pulse_; }
    /** With a loop playing: the loop pulse the next one on a multiple of every will be, in
     *  the direction it plays (an estimate: it may turn, pause or jump on the way) */
    uint32_t NextLoopPulseOn(uint32_t every) const
    {
        if (dir_ < 0)
            return loop_idx_ - loop_idx_ % every; // the next pulse crosses loop_idx_
        const uint32_t next = (loop_idx_ + 1) % loop_pulses_;
        return (next + (every - next % every) % every) % loop_pulses_;
    }

    /** The FX's tempo, whole BPM */
    inline int GetTempo() const { return tempo_; }
    /** The FX's tempo for their times (delay, freezer, tape stop): a loop's exact one, so
     *  the echoes stay on its beats however long it is; else the whole BPM */
    inline float GetFxBpm() const { return HasLoop() ? loop_fx_bpm_ : static_cast<float>(tempo_); }
    /** Whether the pulses count down: a loop playing in reverse */
    inline bool Reverse() const { return HasLoop() && !paused_ && dir_ < 0; }

private:
    /** Without a loop: the tempo, rounded to whole BPM so the FX's times don't wobble */
    void SetFreeTempo(float bpm)
    {
        tempo_ = static_cast<int>(ClampBpm(bpm) + .5f);
        bpm_ = static_cast<float>(tempo_);
        tempo_set_ = true;
    }

    /** A clock's tempo to the FX, from its smoothed period's tempo (smoothed): the line
     *  through the ticks counted since the older of two starts kTempoSpan apart, so over
     *  2-4 s, whose error shrinks with the span (the ticks' jitter doesn't add up, a late
     *  tick pulls a line through hundreds only a little) where the smoothed period's grows
     *  with the tempo (block and USB frame jitter on a short period). A smoothed tempo
     *  further off than its jitter is a new tempo: followed at once, and counted anew. */
    void FollowClock(float smoothed)
    {
        const uint32_t ticks = midi_clock_->GetTicks();
        const uint32_t at = midi_clock_->GetLastTickTime();
        const uint32_t locks = midi_clock_->GetLocks();
        if (counting_ && locks == count_locks_ && ticks != count_last_)
        {
            count_[0].Add(ticks, at);
            count_[1].Add(ticks, at);
            count_last_ = ticks;
        }
        const float counted = at - count_[0].at0 >= kTempoMinSpan
                                  ? 60.f * sample_rate_ / (kTicksPerBeat * count_[0].Period())
                                  : smoothed;
        if (!counting_ || locks != count_locks_ || fabsf(smoothed - counted) > ClockJitter(counted))
        {
            count_[0].Start(ticks, at);
            count_[1] = count_[0];
            count_locks_ = locks;
            count_last_ = ticks;
            counting_ = true;
            SetFreeTempo(smoothed);
            return;
        }
        if (at - count_[1].at0 >= kTempoSpan)
        {
            count_[0] = count_[1];
            count_[1].Start(ticks, at);
        }
        if (fabsf(ClampBpm(counted) - static_cast<float>(tempo_)) > .5f + kTempoMargin)
            SetFreeTempo(counted);
    }

    /** How far a clock's smoothed tempo strays at bpm, at most, from a DAW over USB: its
     *  period is a few blocks at 300 BPM, so it grows with the tempo squared (3 BPM there) */
    static float ClockJitter(float bpm)
    {
        const float r = bpm / static_cast<float>(kMaxBpm);
        return 1.f + 4.f * r * r;
    }

    /** The internal phase advanced by a block at bpm: the pulses it crossed */
    uint32_t FreePulses(size_t size, float bpm)
    {
        phase_ += static_cast<float>(size) * bpm * kPulsesPerBeat / (60.f * sample_rate_);
        const uint32_t pulses = static_cast<uint32_t>(phase_);
        phase_ -= static_cast<float>(pulses);
        return pulses;
    }

    void SetLoopBeats(uint32_t beats)
    {
        loop_beats_ = beats < 1 ? 1 : beats;
        loop_pulses_ = loop_beats_ * kPulsesPerBeat;
        loop_bpm_ = LoopBpm(loop_length_, sample_rate_, loop_beats_);
    }

    inline uint32_t PulseIndex(float pos) const
    {
        const uint32_t idx = static_cast<uint32_t>(pos * loop_pulses_);
        return idx < loop_pulses_ ? idx : loop_pulses_ - 1;
    }

    /** The pulses the play position crossed since the last block, in its direction; while
     *  paused, the pulses of the grid running on by itself */
    uint32_t ProcessLoop(size_t size, float pos, float speed, bool paused)
    {
        loop_pos_ = pos;
        const float bpm = ClampBpm(loop_bpm_ * fabsf(speed));
        tempo_ = static_cast<int>(bpm + .5f);
        loop_fx_bpm_ = bpm;
        had_clock_ = false;
        // the pulses' real rate: paused, the grid's own; playing, the loop's, unclamped
        pulse_bpm_ = paused ? bpm : loop_bpm_ * fabsf(speed);

        if (paused)
        {
            if (!paused_)
            {
                // on from where the loop stopped, the next pulse when it would have come
                paused_ = true;
                free_idx_ = loop_idx_;
                const float at = pos * loop_pulses_;
                phase_ = at - floorf(at);
            }
            return FreePulses(size, bpm);
        }
        if (paused_)
        {
            // resumed: back on the loop's position, which the next pulse brings
            paused_ = false;
            loop_idx_ = PulseIndex(pos);
        }

        const int32_t pulses = static_cast<int32_t>(loop_pulses_);
        int32_t delta = static_cast<int32_t>(PulseIndex(pos)) - static_cast<int32_t>(loop_idx_);
        // across the loop point: the short way round
        if (delta > pulses / 2)
            delta -= pulses;
        else if (delta < -pulses / 2)
            delta += pulses;
        // kept between pulses: a block that crosses none says nothing about the direction
        if (delta != 0)
            dir_ = delta < 0 ? -1 : 1;
        return static_cast<uint32_t>(delta < 0 ? -delta : delta);
    }

    float sample_rate_;
    MidiClock* midi_clock_;
    float bpm_;     // without a loop: the clock's or the free tempo
    int tempo_;     // what the FX get
    bool tempo_set_;
    float phase_;
    bool had_clock_;
    uint32_t last_ticks_;
    uint32_t pulse_count_; // the position, mod kPulsesPerCycle

    /** A least-squares line through the ticks' arrival times since a start: the period */
    struct TickLine
    {
        uint32_t ticks0, at0;
        double n, x, y, xy, xx; // sums of the ticks and times since the start

        void Start(uint32_t ticks, uint32_t at)
        {
            ticks0 = ticks;
            at0 = at;
            n = 1.;
            x = y = xy = xx = 0.;
        }
        void Add(uint32_t ticks, uint32_t at)
        {
            const double dx = static_cast<double>(ticks - ticks0);
            const double dy = static_cast<double>(at - at0);
            n += 1.;
            x += dx;
            y += dy;
            xy += dx * dy;
            xx += dx * dx;
        }
        /** Samples per tick */
        float Period() const { return static_cast<float>((n * xy - x * y) / (n * xx - x * x)); }
    };

    // a clock's ticks counted for its tempo (FollowClock): from two starts, the older first
    bool counting_ = false;
    uint32_t count_locks_ = 0;
    uint32_t count_last_ = 0; // the ticks at the last block that had any
    TickLine count_[2];

    // the loop, while there is one (loop_length_ > 0)
    size_t loop_length_;
    uint32_t loop_beats_;
    uint32_t loop_pulses_; // per pass
    float loop_bpm_;       // at 1x
    float loop_pos_ = 0.f;
    uint32_t loop_idx_;    // the pulse interval the position was last counted in
    int32_t dir_;          // the pulses' direction, -1 in reverse
    bool paused_ = false;  // the loop is paused: the grid runs on by itself
    float pulse_bpm_ = kDefaultBpm; // the loop's pulses' real rate (PulseSamples)
    float loop_fx_bpm_ = kDefaultBpm; // the loop's tempo at its speed, clamped (GetFxBpm)
    uint32_t free_idx_ = 0; // while paused, the grid's last pulse
    uint32_t loop_pulse_ = 0; // the last pulse's place in the loop (LoopPulse)
};

} // namespace chompi
