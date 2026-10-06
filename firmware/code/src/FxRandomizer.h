/** @file FxRandomizer.h
 *  @brief The randomizer: a key that makes no sound of its own but plays the inserts. While
 *  it's on, it steps through a one-bar pattern of 16 sixteenths on the FX clock, and each
 *  gate in it turns 1-5 random effects from the pool on, every knob of each set at random,
 *  for the gate's length. Two gates next to each other are two gates: the second picks again.
 *
 *  Its knobs: pattern (kRandPatterns, sparse to dense), pulse width (the gate as a share of its
 *  16th, at least kMinGateMs), chance (each gate's), shift (every gate later by up to half a
 *  16th). Not part of the scenes: its knobs are kept with the compressor's (MasterSettings.h).
 *
 *  The pool is every insert but the freezer, and the resonator; not the sends (FxChain's
 *  kRandomPool). An effect whose key is held or latched isn't picked: it keeps its knobs. Nor is one whose last random
 *  gate has only just closed (kCoolMs), so it's silent again when its knobs jump. FxChain
 *  hands the effects over and gives the user's knobs back afterwards.
 *
 *  This is the engine's side, run in the audio callback; no hardware, so test/randomizer.cpp
 *  runs it on the host.
 */
#pragma once
#include <stdint.h>
#include "FxCommon.h"
#include "TempoClock.h"

namespace chompi
{

// The randomizer's patterns (knob 1), one bar each: bit 15 is the first 16th, bit 0 the last
static const uint16_t kRandPatterns[] = {
    0b1000000000000000, // x . . . | . . . . | . . . . | . . . .
    0b1000000010000000, // x . . . | . . . . | x . . . | . . . .
    0b1000100010001000, // quarters
    0b1000001000001000, // x . . . | . . x . | . . . . | x . . .
    0b1001001001001001, // x . . x | . . x . | . x . . | x . . x
    0b1000100010010000, // x . . . | x . . . | x . . x | . . . .
    0b1000100010101000, // x . . . | x . . . | x . x . | x . . .
    0b0010001000110010, // . . x . | . . x . | . . x x | . . x .
    0b1000100101101000, // x . . . | x . . x | . x x . | x . . .
    0b1110000010000000, // x x x . | . . . . | x . . . | . . . .
    0b1100110010001000, // x x . . | x x . . | x . . . | x . . .
    0b1000110011101111, // x . . . | x x . . | x x x . | x x x x
    0b1001100110011001, // x . . x | x . . x | x . . x | x . . x
    0b1001001000101000, // x . . x | . . x . | . . x . | x . . .
    0b0111011101110111, // . x x x | . x x x | . x x x | . x x x
    0b1111111100000000, // 16ths for half a bar
    0b1111101010111010, // x x x x | x . x . | x . x x | x . x .
};

class Randomizer
{
public:
    enum Param
    {
        PATTERN,
        WIDTH,
        CHANCE,
        SHIFT,
    };

    static const size_t kNumPatterns = 17;
    static const size_t kNumSteps = 16;
    static const size_t kMaxPicks = 5;
    static constexpr float kMinGateMs = 20.f; // the punch-in fade needs ~15ms to come in
    static constexpr float kCoolMs = 25.f;    // the punch-in fade out (FxGate), settled

    /** pool: the effects it may pick, a bit per FxId */
    void Init(float sample_rate, uint16_t pool)
    {
        sample_rate_ = sample_rate;
        pool_ = pool;
        min_gate_ = static_cast<int32_t>(kMinGateMs * .001f * sample_rate);
        cool_ = static_cast<int32_t>(kCoolMs * .001f * sample_rate);
        rng_ = 0x9E3779B9u;
        on_ = was_on_ = false;
        step_ = -1;
        armed_ = false;
        delay_left_ = gate_left_ = 0;
        mask_ = taken_ = 0;
        fires_ = 0;
        last_pick_ = 0;
        for (size_t fx = 0; fx < 16; fx++)
            cool_left_[fx] = 0;
        SetTempo(kDefaultBpm);
        for (size_t p = 0; p < kNumFxParams; p++)
            params_[p] = 0.f;
    }

    /** From the UI: the key held or latched. Taken in the next Block */
    inline void SetOn(bool on) { on_ = on; }
    inline bool IsOn() const { return on_; }
    inline void SetParam(size_t param, float val)
    {
        if (param < kNumFxParams)
            params_[param] = fclamp(val, 0.f, 1.f);
    }

    /** Once per block, before the pulses */
    void SetTempo(int bpm)
    {
        sixteenth_ = sample_rate_ * 15.f / static_cast<float>(bpm > 0 ? bpm : kDefaultBpm);
    }

    /** One per clock pulse (TempoClock::Pulse): every 16th, a gate is armed if the pattern
     *  has one there and its chance comes up, to fire after the shift */
    void ClockPulse(uint32_t pos)
    {
        if (pos % kPulsesPer16th != 0)
            return;
        step_ = static_cast<int32_t>((pos / kPulsesPer16th) % kNumSteps);
        if (on_ && was_on_)
            Arm();
    }

    /** Once per block, after the pulses. manual: the effects whose keys are on, which aren't
     *  picked. Afterwards Mask() is the effects the gate has on, Taken() those it took in this
     *  block, whose knobs are Value(fx, p) */
    void Block(size_t size, uint16_t manual)
    {
        taken_ = 0;
        const int32_t n = static_cast<int32_t>(size);
        for (size_t fx = 0; fx < 16; fx++)
            if (cool_left_[fx] > 0)
                cool_left_[fx] -= n;

        const bool on = on_;
        if (on != was_on_)
        {
            was_on_ = on;
            armed_ = false;
            if (on)
            {
                // joins the bar where it is: a gate on this 16th fires now
                if (step_ >= 0)
                    Arm();
            }
            else
                Close();
        }
        if (!on)
            return;

        if (gate_left_ > 0)
        {
            gate_left_ -= n;
            if (gate_left_ <= 0)
                Close();
        }
        if (armed_)
        {
            delay_left_ -= n;
            if (delay_left_ <= 0)
            {
                armed_ = false;
                Fire(manual);
            }
        }
    }

    /** An effect's key came on while the gate has it: it's the user's from now, at once */
    void Drop(size_t fx)
    {
        const uint16_t bit = static_cast<uint16_t>(1u << fx);
        mask_ &= static_cast<uint16_t>(~bit);
        taken_ &= static_cast<uint16_t>(~bit);
        cool_left_[fx] = 0;
    }

    /** Off, and Block has seen it: nothing open, nothing armed */
    inline bool Idle() const { return !on_ && !was_on_ && !mask_ && !armed_; }
    inline uint16_t Mask() const { return mask_; }
    inline uint16_t Taken() const { return taken_; }
    /** Its last random gate closed less than kCoolMs ago: still fading out */
    inline bool Cooling(size_t fx) const { return cool_left_[fx] > 0; }
    inline float Value(size_t fx, size_t param) const { return values_[fx][param]; }
    /** For the key LED: counts the gates fired, and the first effect the last one picked */
    inline uint32_t Fires() const { return fires_; }
    inline size_t LastPick() const { return last_pick_; }

private:
    /** The pattern has a gate on this 16th and its chance comes up: armed, to fire after the
     *  shift */
    void Arm()
    {
        const size_t pattern = StepIndex(params_[PATTERN], kNumPatterns);
        if (!((kRandPatterns[pattern] >> (kNumSteps - 1 - static_cast<size_t>(step_))) & 1))
            return;
        if (Uniform() >= params_[CHANCE])
            return;
        armed_ = true;
        delay_left_ = static_cast<int32_t>(params_[SHIFT] * .5f * sixteenth_);
    }

    /** A gate: the last one closes, 1-kMaxPicks effects from what's free come on with random
     *  knobs */
    void Fire(uint16_t manual)
    {
        Close();
        uint16_t free = static_cast<uint16_t>(pool_ & ~manual);
        for (size_t fx = 0; fx < 16; fx++)
            if (cool_left_[fx] > 0)
                free &= static_cast<uint16_t>(~(1u << fx));

        const size_t picks = 1 + Next() % kMaxPicks;
        for (size_t i = 0; i < picks && free; i++)
        {
            // the k-th effect still free
            size_t k = Next() % static_cast<uint32_t>(__builtin_popcount(free));
            size_t fx = 0;
            for (;; fx++)
                if (((free >> fx) & 1) && k-- == 0)
                    break;
            const uint16_t bit = static_cast<uint16_t>(1u << fx);
            free &= static_cast<uint16_t>(~bit);
            mask_ |= bit;
            taken_ |= bit;
            for (size_t p = 0; p < kNumFxParams; p++)
                values_[fx][p] = Uniform();
            if (i == 0)
                last_pick_ = fx;
        }
        if (mask_)
            fires_++;
        const int32_t gate = static_cast<int32_t>(params_[WIDTH] * sixteenth_);
        gate_left_ = gate > min_gate_ ? gate : min_gate_;
    }

    /** The gate closes: its effects go off and fade out */
    void Close()
    {
        for (size_t fx = 0; fx < 16; fx++)
            if ((mask_ >> fx) & 1)
                cool_left_[fx] = cool_;
        mask_ = 0;
        gate_left_ = 0;
    }

    uint32_t Next()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }
    /** 0..1, never 1 */
    inline float Uniform() { return static_cast<float>(Next() >> 8) * (1.f / 16777216.f); }

    float sample_rate_;
    uint16_t pool_;
    float sixteenth_;   // samples
    int32_t min_gate_;  // samples, kMinGateMs
    int32_t cool_;      // samples, kCoolMs
    float params_[kNumFxParams];
    uint32_t rng_;
    volatile bool on_;
    bool was_on_;       // what Block last saw of on_
    int32_t step_;      // the 16th the clock's on, -1 before its first
    bool armed_;        // a gate waiting out the shift
    int32_t delay_left_;
    int32_t gate_left_; // samples until the gate closes, 0: none open
    uint16_t mask_;
    uint16_t taken_;
    int32_t cool_left_[16];
    float values_[16][kNumFxParams];
    uint32_t fires_;
    size_t last_pick_;
};

static_assert(sizeof(kRandPatterns) / sizeof(kRandPatterns[0]) == Randomizer::kNumPatterns,
              "one per pattern");

} // namespace chompi
