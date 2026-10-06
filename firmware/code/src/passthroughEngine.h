/** @file passthroughEngine.h
 *  @brief Audio engine: the stereo AUX input goes to both outputs through the Volume
 *  Engine: input gain -> input/loop mix -> punch-in FX (FxChain.h) -> output gain -> master
 *  compressor.
 *
 *  In the code the mix is dry/wet: dry is the input on its own, wet is the looper's playback
 *  on its own. The looper records the dry signal (see Looper.h).
 *
 *  The input level, output level and compressor stage are ported from TAPE's DSPEngine
 *  so the gains match the hardware the way TAPE tuned them.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "EnvFollower.h"
#include "FxChain.h"
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
              float* delay_mem, float* delay_frozen_mem, size_t delay_frames,
              daisysp::Reverb* reverb,
              float* freezer_mem_l, float* freezer_mem_r, size_t freezer_frames)
    {
        looper.Init(loop_mem, midi_clock);
        tempo_clock_.Init(sample_rate, midi_clock);
        fx_.Init(sample_rate, delay_mem, delay_frozen_mem, delay_frames, reverb,
                 freezer_mem_l, freezer_mem_r, freezer_frames);

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

        // the FX's tempo and clock, once per block
        const uint32_t pulses = tempo_clock_.Process(size);
        fx_.SetTempo(tempo_clock_.GetTempo());
        for (uint32_t p = 0; p < pulses; p++)
            fx_.ClockPulse(tempo_clock_.Pulse());

        for (size_t i = 0; i < size; i++)
        {
            fonepole(mgain_, mgain_target_, .001f);
            fonepole(final_lim_, final_lim_target_, .001f);
            fonepole(mix_, mix_target_, .001f);

            // equal-power crossfade so the middle of the knob doesn't dip in level
            const float dry_amt = cosf(mix_ * HALFPI_F);
            const float wet_amt = sinf(mix_ * HALFPI_F);
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
        }
    }

    inline void SetMainGain(float gain) { mgain_target_ = gain; }
    inline void SetInputGain(float gain) { ingain_target_ = gain; }
    inline void SetFinalComp(float comp) { final_lim_target_ = comp; }
    /** 0 = dry (input only), 1 = wet (looper/buffer only) */
    inline void SetMix(float mix) { mix_target_ = mix; }

    /** Punch-in FX, by FxId (FxChain.h) */
    inline void SetFxOn(size_t fx, bool on) { fx_.SetOn(fx, on); }
    inline void SetFxParam(size_t fx, size_t param, float val) { fx_.SetParam(fx, param, val); }
    /** Before a scene recall's SetFxParams: they land at the recall's slew (FxCommon.h) */
    inline void FastFxSlew() { fx_.FastSlew(); }
    /** 0..1, for the FX key LEDs: an insert's output, a send's return */
    inline float GetFxLevel(size_t fx) { return fx_.GetLevel(fx); }

    inline float GetVUSample() { return output_env_follower.GetLastSamp(); }

    chompi::Looper looper;

private:
    daisysp::DcBlock dcblock_line_in_l_, dcblock_line_in_r_;
    chompi::Limiter lim_hp_l_, lim_hp_r_, lim_line_l_, lim_line_r_;
    chompi::EnvFollower output_env_follower;
    chompi::TempoClock tempo_clock_;
    chompi::FxChain fx_;
    // live values start at 0 and slew up to the targets the play page sets at boot
    float mgain_ = 0.f, mgain_target_ = 0.f;
    float ingain_ = 0.f, ingain_target_ = 0.f;
    float final_lim_ = 0.f, final_lim_target_ = 0.f;
    float mix_ = 0.f, mix_target_ = 0.f;
};
