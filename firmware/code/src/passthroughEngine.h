/** @file passthroughEngine.h
 *  @brief Audio engine: the stereo AUX input goes to both outputs through the Volume
 *  Engine: input gain -> input/loop mix -> punch-in FX (FxChain.h) -> master compressor
 *  (MasterComp.h) -> output gain -> safety limiter.
 *
 *  In the code the mix is dry/wet: dry is the input on its own, wet is the looper's playback
 *  on its own. The looper records the dry signal (see Looper.h).
 *
 *  The headphones mirror the master out, or with SetHeadphoneCue() blend in the input on its
 *  own: after the input gain and VOLUME, but no loop, no FX, no MIX and no compressor. The
 *  headphones' safety limiter comes after that blend, so it guards the input too.
 *
 *  The FX's tempo (TempoClock.h) comes from the loop while there is one, otherwise from MIDI
 *  clock or taps. A scene morph (FxMorph.h) sits between the UI and the FX and lands on that
 *  clock's bar lines.
 *
 *  The input level, output level and safety limiter are ported from TAPE's DSPEngine
 *  so the gains match the hardware the way TAPE tuned them. The limiter is TAPE's master
 *  compressor at its lowest setting, which is what FRIZZ's own knob for it started at.
 */
#pragma once
#include <atomic>
#include "daisy.h"
#include "daisysp.h"
#include "EnvFollower.h"
#include "FxChain.h"
#include "FxMorph.h"
#include "limiter.h"
#include "Looper.h"
#include "MasterComp.h"
#include "TempoClock.h"

using namespace daisy;
using namespace daisysp;

static constexpr float kLineOutGain = .3f;
static constexpr float kHpGain = .2f;
static constexpr float kLineInGain = 3.f;
// the safety limiter's threshold and makeup (limiter.h)
static constexpr float kLimThresh = .25f;
static constexpr float kLimMakeup = .9f;

class PassthroughEngine
{
public:

    PassthroughEngine() {};
    ~PassthroughEngine() {};

    void Init(float sample_rate, int16_t* loop_mem, chompi::MidiClock* midi_clock,
              float* delay_mem, size_t delay_frames,
              daisysp::Reverb* reverb,
              float* freezer_mem_l, float* freezer_mem_r, size_t freezer_frames,
              float* tapestop_mem_l, float* tapestop_mem_r, size_t tapestop_frames)
    {
        sample_rate_ = sample_rate;
        looper.Init(loop_mem, midi_clock);
        tempo_clock_.Init(sample_rate, midi_clock);
        fx_.Init(sample_rate, delay_mem, delay_frames, reverb,
                 freezer_mem_l, freezer_mem_r, freezer_frames,
                 tapestop_mem_l, tapestop_mem_r, tapestop_frames);
        morph_.Init(&fx_);
        comp_.Init(sample_rate);

        dcblock_line_in_l_.Init(sample_rate);
        dcblock_line_in_r_.Init(sample_rate);

        lim_hp_l_.Init();
        lim_hp_r_.Init();
        lim_line_l_.Init();
        lim_line_r_.Init();

        output_env_follower.Init();
    }

    /** Inputs: 0 mic (unused), 1 X, 2 aux L, 3 aux R
     *  Outputs: 0/1 headphone L/R, 2/3 master L/R */
    void Process(const float *const *in, float **out, size_t size)
    {
        float dryl[size], dryr[size], wetl[size], wetr[size];

        for (size_t i = 0; i < size; i++)
        {
            // Every setting has a target and a live value; fonepole() slews the live
            // value toward the target (a ~21ms time constant) so knob turns don't zipper.
            fonepole(ingain_, ingain_target_, .001f);

            // mono: a TS plug grounds the right channel, so the left (its tip) feeds both
            const float in_r = mono_in_ ? in[2][i] : in[3][i];
            dryl[i] = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
            dryr[i] = dcblock_line_in_r_.Process(in_r * ingain_ * kLineInGain);
        }

        looper.Process(dryl, dryr, wetl, wetr, size);

        // the FX's tempo and clock, once per block, from the loop while there is one
        SyncLoopTempo();
        const float tap = tap_bpm_.exchange(0.f);
        if (tap > 0.f)
            tempo_clock_.Tap(tap);
        const uint32_t pulses = tempo_clock_.Process(
            size, looper.GetPosition(), looper.GetActualSpeed(),
            looper.GetState() == chompi::Looper::State::PAUSED);
        fx_.SetTempo(tempo_clock_.GetFxBpm(), tempo_clock_.PulseSamples());
        for (uint32_t p = 0; p < pulses; p++)
        {
            const uint32_t pos = tempo_clock_.Pulse();
            fx_.ClockPulse(pos, tempo_clock_.Reverse());
            morph_.Pulse(chompi::TempoClock::IsBarLine(pos));
        }
        morph_.Process(size, tempo_clock_.PulseSamples());

        for (size_t i = 0; i < size; i++)
        {
            fonepole(mgain_, mgain_target_, .001f);
            // equal-power crossfade so the middle of the knob doesn't dip in level; the
            // cosf and sinf only while the knob slews, the mix usually sits still
            if (mix_ != mix_target_)
            {
                fonepole(mix_, mix_target_, .001f);
                if (fabsf(mix_ - mix_target_) < 1e-5f)
                    mix_ = mix_target_;
                dry_amt_ = cosf(mix_ * HALFPI_F);
                wet_amt_ = sinf(mix_ * HALFPI_F);
            }
            const float dry_amt = dry_amt_;
            const float wet_amt = wet_amt_;
            float sigl = dryl[i] * dry_amt + wetl[i] * wet_amt;
            float sigr = dryr[i] * dry_amt + wetr[i] * wet_amt;

            // punch-in FX, on the mix so they work on the input, the loop or both, and
            // before the output gain so they don't change with the VOLUME knob
            fx_.Process(&sigl, &sigr);
            comp_.Process(&sigl, &sigr);

            // headphone and master gain
            const float hpl = sigl * kHpGain * mgain_;
            const float hpr = sigr * kHpGain * mgain_;
            out[2][i] = sigl * kLineOutGain * mgain_;
            out[3][i] = sigr * kLineOutGain * mgain_;

            // the VU meter shows the master's signal (at the headphones' level, before the
            // limiter), whatever the headphones carry
            output_env_follower.Process(hpl + hpr);

            // headphone feed: the cue blends from the master's mirror to the input on its own,
            // slewed so a jump doesn't click
            fonepole(hp_cue_, hp_cue_target_, .001f);
            out[0][i] = hpl + (dryl[i] * kHpGain * mgain_ - hpl) * hp_cue_;
            out[1][i] = hpr + (dryr[i] * kHpGain * mgain_ - hpr) * hp_cue_;

            // safety limiter: TAPE's master compressor at its lowest setting (limiter.h)
            out[0][i] = lim_hp_l_.ProcessComp(out[0][i], 1.f, kLimThresh, 1.f, kLimMakeup);
            out[1][i] = lim_hp_r_.ProcessComp(out[1][i], 1.f, kLimThresh, 1.f, kLimMakeup);
            out[2][i] = lim_line_l_.ProcessComp(out[2][i], 1.f, kLimThresh, 1.f, kLimMakeup);
            out[3][i] = lim_line_r_.ProcessComp(out[3][i], 1.f, kLimThresh, 1.f, kLimMakeup);
        }
    }

    inline void SetMainGain(float gain) { mgain_target_ = gain; }
    inline void SetInputGain(float gain) { ingain_target_ = gain; }
    /** The master compressor's knobs (MasterComp.h), 0..1 */
    inline void SetCompParam(size_t param, float val) { comp_.SetParam(param, val); }
    /** Its gain reduction now, in dB (<= 0) */
    inline float GetCompReduction() const { return comp_.GetReduction(); }
    /** 0 = dry (input only), 1 = wet (looper/buffer only) */
    inline void SetMix(float mix) { mix_target_ = mix; }
    /** Headphones: 0 = mirror the master out, 1 = the dry input on its own */
    inline void SetHeadphoneCue(float cue) { hp_cue_target_ = cue; }
    /** AUX input: false = stereo, true = mono, the left channel to both sides */
    inline void SetMonoInput(bool mono) { mono_in_ = mono; }

    /** Punch-in FX, by FxId (FxChain.h). While a morph runs, they go to it (FxMorph.h) */
    inline void SetFxOn(size_t fx, bool on)
    {
        if (!morph_.SetOn(fx, on))
            fx_.SetOn(fx, on);
    }
    inline void SetFxParam(size_t fx, size_t param, float val)
    {
        if (!morph_.SetParam(fx, param, val))
            fx_.SetParam(fx, param, val);
    }
    /** Before a scene recall's SetFxParams: they land at the recall's slew (FxCommon.h) */
    inline void FastFxSlew() { fx_.FastSlew(); }
    /** A scene morph (FxMorph.h) to the next bar line of the FX's clock, one more per
     *  AddFxMorphBar; held until ReleaseFxMorph (SHIFT let go); LandFxMorph ends it at once.
     *  All with the audio interrupt blocked */
    void StartFxMorph(const chompi::FxMorphPlan& plan)
    {
        morph_.Start(plan, tempo_clock_.PulsesToBarLine(), true);
    }
    void ReleaseFxMorph()
    {
        morph_.Release(tempo_clock_.PulsesToBarLine(), tempo_clock_.PulsesPerBarLine());
    }
    bool AddFxMorphBar() { return morph_.AddBar(tempo_clock_.PulsesPerBarLine()); }
    void LandFxMorph() { morph_.Land(); }
    /** Stops the morph where it is (FxMorph::Freeze); with the audio interrupt blocked */
    bool FreezeFxMorph(float params[chompi::kNumFx][chompi::kNumFxParams],
                       uint16_t* unswitched, uint16_t* was_on)
    {
        return morph_.Freeze(params, unswitched, was_on);
    }
    inline bool FxMorphing() const { return morph_.Active(); }
    /** The FX clock's position, 0..TempoClock's kPulsesPerCycle - 1, for blinking on its beats */
    inline uint32_t FxClockPosition() const { return tempo_clock_.Position(); }
    /** 0..1, for the FX key LEDs: an insert's output, a send's return */
    inline float GetFxLevel(size_t fx) { return fx_.GetLevel(fx); }

    inline float GetVUSample() { return output_env_follower.GetLastSamp(); }

    /** A tapped tempo, from the UI (TapTempo.h): applied at the next block */
    inline void TapTempo(float bpm) { tap_bpm_.store(bpm); }
    /** Whether a tap would be taken: not without a loop while MIDI clock runs */
    inline bool CanTap() const { return tempo_clock_.CanTap(); }

    chompi::Looper looper;

private:
    /** Hands a loop that just closed to the tempo clock, and takes an erased one away. A
     *  quantized loop knows its beats; an unquantized one fits the tempo set before it, or
     *  is guessed */
    void SyncLoopTempo()
    {
        const chompi::Looper::State state = looper.GetState();
        const bool loop = state == chompi::Looper::State::PLAYING
                          || state == chompi::Looper::State::PAUSED;
        if (loop && !tempo_clock_.HasLoop())
        {
            const size_t length = looper.GetLength();
            uint32_t beats = looper.GetBeats();
            if (beats == 0)
                beats = tempo_clock_.TempoSet()
                            ? chompi::FitBeats(length, sample_rate_, tempo_clock_.GetBpm())
                            : chompi::GuessBeats(length, sample_rate_);
            tempo_clock_.SetLoop(length, beats);
        }
        else if (!loop && tempo_clock_.HasLoop())
            tempo_clock_.ClearLoop();
    }

    float sample_rate_;
    std::atomic<float> tap_bpm_{0.f};
    daisysp::DcBlock dcblock_line_in_l_, dcblock_line_in_r_;
    chompi::Limiter lim_hp_l_, lim_hp_r_, lim_line_l_, lim_line_r_;
    chompi::EnvFollower output_env_follower;
    chompi::TempoClock tempo_clock_;
    chompi::FxChain fx_;
    chompi::FxMorph morph_;
    chompi::MasterComp comp_;
    volatile bool mono_in_ = false;
    // live values start at 0 and slew up to the targets the play page sets at boot
    float mgain_ = 0.f, mgain_target_ = 0.f;
    float ingain_ = 0.f, ingain_target_ = 0.f;
    float mix_ = 0.f, mix_target_ = 0.f;
    float dry_amt_ = 1.f, wet_amt_ = 0.f; // the crossfade at mix_
    float hp_cue_ = 0.f, hp_cue_target_ = 0.f;
};
