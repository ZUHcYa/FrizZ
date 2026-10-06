/** @file passthroughEngine.h
 *  @brief Audio engine: the stereo AUX input goes to both outputs through the Volume
 *  Engine: input gain -> input/loop mix -> punch-in FX (FxChain.h) -> output gain -> master
 *  compressor.
 *
 *  In the code the mix is dry/wet: dry is the input on its own, wet is the looper's playback
 *  on its own. The looper records the dry signal (see Looper.h).
 *
 *  The headphones mirror the master out, or with SetHeadphoneDry() carry the input on its
 *  own: after the input gain and VOLUME, but no loop, no FX, no MIX and no compressor.
 *
 *  The FX's tempo (TempoClock.h) comes from the loop while there is one, otherwise from MIDI
 *  clock or taps. A scene morph (FxMorph.h) sits between the UI and the FX and lands on that
 *  clock's bar lines.
 *
 *  The input level, output level and compressor stage are ported from TAPE's DSPEngine
 *  so the gains match the hardware the way TAPE tuned them.
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
#include "TempoClock.h"

using namespace daisy;
using namespace daisysp;

static constexpr float kLineOutGain = .3f;
static constexpr float kHpGain = .2f;
static constexpr float kLineInGain = 3.f;

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
            // value toward the target over ~1ms so knob turns don't zipper.
            fonepole(ingain_, ingain_target_, .001f);

            dryl[i] = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
            dryr[i] = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);
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
        fx_.SetTempo(tempo_clock_.GetTempo());
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
            fonepole(final_lim_, final_lim_target_, .001f);
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

            // headphone and master gain
            out[0][i] = sigl * kHpGain * mgain_;
            out[1][i] = sigr * kHpGain * mgain_;
            out[2][i] = sigl * kLineOutGain * mgain_;
            out[3][i] = sigr * kLineOutGain * mgain_;

            // master compressor
            const float thresh = 1.f / (10.f * final_lim_ + 4.f);
            const float ratio = 1.f + final_lim_ * final_lim_ * 7.f;
            const float makeup = .9f + final_lim_ * .6f;
            const float pregain = 7.f * final_lim_ + 1.f;
            out[0][i] = lim_hp_l_.ProcessComp(out[0][i], pregain, thresh, ratio, makeup);
            out[1][i] = lim_hp_r_.ProcessComp(out[1][i], pregain, thresh, ratio, makeup);
            out[2][i] = lim_line_l_.ProcessComp(out[2][i], pregain, thresh, ratio, makeup);
            out[3][i] = lim_line_r_.ProcessComp(out[3][i], pregain, thresh, ratio, makeup);

            output_env_follower.Process((out[0][i] + out[1][i]));

            // headphone feed, after the VU meter so it keeps metering the master's signal;
            // crossfaded so switching doesn't click
            fonepole(hp_dry_, hp_dry_target_, .001f);
            out[0][i] += (dryl[i] * kHpGain * mgain_ - out[0][i]) * hp_dry_;
            out[1][i] += (dryr[i] * kHpGain * mgain_ - out[1][i]) * hp_dry_;
        }
    }

    inline void SetMainGain(float gain) { mgain_target_ = gain; }
    inline void SetInputGain(float gain) { ingain_target_ = gain; }
    inline void SetFinalComp(float comp) { final_lim_target_ = comp; }
    /** 0 = dry (input only), 1 = wet (looper/buffer only) */
    inline void SetMix(float mix) { mix_target_ = mix; }
    /** Headphones: false = mirror the master out, true = the dry input on its own */
    inline void SetHeadphoneDry(bool dry) { hp_dry_target_ = dry ? 1.f : 0.f; }

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
     *  AddFxMorphBar; LandFxMorph ends it at once. All with the audio interrupt blocked */
    void StartFxMorph(const chompi::FxMorphPlan& plan)
    {
        morph_.Start(plan, tempo_clock_.PulsesToBarLine());
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
    // live values start at 0 and slew up to the targets the play page sets at boot
    float mgain_ = 0.f, mgain_target_ = 0.f;
    float ingain_ = 0.f, ingain_target_ = 0.f;
    float final_lim_ = 0.f, final_lim_target_ = 0.f;
    float mix_ = 0.f, mix_target_ = 0.f;
    float dry_amt_ = 1.f, wet_amt_ = 0.f; // the crossfade at mix_
    float hp_dry_ = 0.f, hp_dry_target_ = 0.f;
};
