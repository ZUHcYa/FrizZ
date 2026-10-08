/** @file clockgen.h
 *  @brief A MIDI clock out of a virtual sequencer, into the twin's jack or over USB: 24 ticks
 *  a beat at a tempo, with what real senders add to it. Used by the script player (`clock`)
 *  and the timing checks (../test/sync.cpp, midi.cpp).
 *
 *  - jitter: each tick sent up to that many ms early or late (evenly spread, never before the
 *    tick before it), a hardware sequencer's timer or a DAW's scheduling;
 *  - drift: the sender's clock off by that many ppm, so its 120 BPM is a little slower (+) or
 *    faster (-) than CHOMPI's crystal says;
 *  - USB: the ticks go out in the host's 1 ms USB frames, each at the start of the frame after
 *    it was due, so up to 1 ms late and sometimes two in a frame;
 *  - a ramp: the tempo moves linearly to another over some ms.
 *
 *  Deterministic: the jitter comes from a seeded generator of its own.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include "twin.h"

namespace twin
{
class ClockGen
{
public:
    struct Config
    {
        double bpm = 120.;
        double jitter_ms = 0.;
        double drift_ppm = 0.;
        bool usb = false;
        double ramp_to = 0.; // a tempo to move to, 0 for none
        double ramp_ms = 0.;
        uint32_t seed = 1;
    };

    /** Starts at now_ms (BlockMs()): the first tick goes out then */
    void Start(const Config& c, double now_ms)
    {
        cfg_ = c;
        start_ms_ = now_ms;
        next_ideal_ = now_ms;
        last_send_ = now_ms;
        rand_ = c.seed ? c.seed : 1;
        sent_ = 0;
        running_ = c.bpm > 0.;
        Schedule();
    }
    void Stop() { running_ = false; }
    inline bool Running() const { return running_; }
    inline uint64_t Sent() const { return sent_; }
    inline const Config& Cfg() const { return cfg_; }

    /** The tempo the sender plays at now_ms, as CHOMPI's clock measures it (with the drift) */
    double Bpm(double now_ms) const { return NominalBpm(now_ms) / (1. + cfg_.drift_ppm * 1e-6); }
    /** A tick's length at that tempo, in CHOMPI's samples */
    double TickSamples(double now_ms) const { return 60. * 48000. / (Bpm(now_ms) * 24.); }

    /** Sends every tick due by now_ms: call before each block with BlockMs() */
    void Step(double now_ms)
    {
        while (running_ && send_ <= now_ms)
        {
            if (cfg_.usb)
                MidiUsb(0xF8);
            else
                Midi(0xF8);
            sent_++;
            Schedule();
        }
    }

private:
    double NominalBpm(double ms) const
    {
        if (cfg_.ramp_to <= 0. || cfg_.ramp_ms <= 0.)
            return cfg_.bpm;
        const double f = (ms - start_ms_) / cfg_.ramp_ms;
        return f >= 1. ? cfg_.ramp_to : cfg_.bpm + (cfg_.ramp_to - cfg_.bpm) * (f < 0. ? 0. : f);
    }

    double Uniform() // -1..1
    {
        rand_ = rand_ * 1664525u + 1013904223u;
        return (rand_ >> 8) / double(1 << 23) - 1.;
    }

    // the next tick: its ideal time, then when it actually goes out
    void Schedule()
    {
        const double ideal = next_ideal_;
        next_ideal_ += 60000. / (Bpm(ideal) * 24.);
        double t = ideal + cfg_.jitter_ms * Uniform();
        if (cfg_.usb)
            t = start_ms_ + kFramePhase + std::ceil((t - start_ms_ - kFramePhase) / 1.) * 1.;
        send_ = t < last_send_ ? last_send_ : t;
        last_send_ = send_;
    }

    static constexpr double kFramePhase = .37; // where the host's frames start, against the blocks

    Config cfg_;
    double start_ms_ = 0., next_ideal_ = 0., send_ = 0., last_send_ = 0.;
    uint32_t rand_ = 1;
    uint64_t sent_ = 0;
    bool running_ = false;
};
} // namespace twin
