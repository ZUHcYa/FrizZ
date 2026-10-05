/** @file TempoClock.h
 *  @brief The tempo and clock pulses for the delay send (granularDelay.h).
 *
 *  With MIDI clock, the tempo follows MidiClock rounded to whole BPM (so the delay time
 *  doesn't wobble with the clock's jitter), and pulses are counted from the incoming ticks,
 *  one pulse per 2 ticks (12 PPQN, what TEMPO's delay counts in). Without MIDI clock, the last
 *  tempo is kept (120 BPM until a clock has been seen) and pulses come from an internal phase
 *  at that tempo.
 *
 *  Every 6th pulse is an 8th-note edge, where the delay rolls its random events.
 *
 *  Runs in the audio callback, once per block.
 */
#pragma once
#include "daisy.h"
#include "MidiClock.h"

namespace chompi
{

static const int kDefaultBpm = 120;
// 2 bars plus the delay's stereo offset must fit in its 10s buffer, which needs > 48 BPM
static const int kMinBpm = 50;
static const int kMaxBpm = 300;
static const uint32_t kTicksPerPulse = 2;   // 24 PPQN MIDI ticks -> 12 PPQN pulses
static const uint32_t kPulsesPerEdge = 6;   // 8th notes

class TempoClock
{
public:
    void Init(float sample_rate, MidiClock* midi_clock)
    {
        sample_rate_ = sample_rate;
        midi_clock_ = midi_clock;
        tempo_ = kDefaultBpm;
        phase_ = 0.f;
        had_clock_ = false;
        last_ticks_ = 0;
        pulse_count_ = 0;
    }

    /** Call once per block. Returns how many pulses happened during it (usually 0 or 1). */
    uint32_t Process(size_t size)
    {
        uint32_t pulses = 0;
        const bool has_clock = midi_clock_->HasClock();

        if (has_clock)
        {
            // 0 between the first and second tick of a new lock: keep the last tempo
            const float bpm = midi_clock_->GetBpm();
            if (bpm > 0.f)
            {
                const int rounded = static_cast<int>(bpm + .5f);
                tempo_ = rounded < kMinBpm ? kMinBpm : (rounded > kMaxBpm ? kMaxBpm : rounded);
            }

            const uint32_t ticks = midi_clock_->GetTicks();
            if (!had_clock_)
                last_ticks_ = ticks; // new lock: count from here
            pulses = (ticks - last_ticks_) / kTicksPerPulse;
            last_ticks_ += pulses * kTicksPerPulse;
            phase_ = 0.f;
        }
        else
        {
            phase_ += static_cast<float>(size) * static_cast<float>(tempo_) * 12.f / (60.f * sample_rate_);
            pulses = static_cast<uint32_t>(phase_);
            phase_ -= static_cast<float>(pulses);
        }

        had_clock_ = has_clock;
        return pulses;
    }

    /** Count one pulse; true when it falls on an 8th-note edge */
    bool Pulse()
    {
        pulse_count_ = (pulse_count_ + 1) % kPulsesPerEdge;
        return pulse_count_ == 0;
    }

    inline int GetTempo() const { return tempo_; }

private:
    float sample_rate_;
    MidiClock* midi_clock_;
    int tempo_;
    float phase_;
    bool had_clock_;
    uint32_t last_ticks_;
    uint32_t pulse_count_;
};

} // namespace chompi
