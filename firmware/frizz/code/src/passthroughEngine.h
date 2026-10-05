/** @file passthroughEngine.h
 *  @brief Audio engine: the stereo AUX input goes to both outputs through the Volume
 *  Engine: input gain -> dry/wet mix -> output gain -> master compressor.
 *
 *  Dry is the input on its own, wet is the processed signal from the looper/buffer
 *  on its own. The looper doesn't exist yet, so wet is silent for now.
 *
 *  The input level, output level and compressor stage are ported from TAPE's DSPEngine
 *  so the gains match the hardware the way TAPE tuned them.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "EnvFollower.h"
#include "limiter.h"

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

    void Init(float sample_rate)
    {
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
        for (size_t i = 0; i < size; i++)
        {
            // Every setting has a target and a live value; fonepole() slews the live
            // value toward the target over ~1ms so knob turns don't zipper.
            fonepole(mgain_, mgain_target_, .001f);
            fonepole(ingain_, ingain_target_, .001f);
            fonepole(final_lim_, final_lim_target_, .001f);
            fonepole(mix_, mix_target_, .001f);

            const float dryl = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
            const float dryr = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);

            // TODO(frizz): the looper/buffer output goes here (Phase 2)
            const float wetl = 0.f;
            const float wetr = 0.f;

            // equal-power crossfade so the middle of the knob doesn't dip in level
            const float dry_amt = cosf(mix_ * HALFPI_F);
            const float wet_amt = sinf(mix_ * HALFPI_F);
            const float sigl = dryl * dry_amt + wetl * wet_amt;
            const float sigr = dryr * dry_amt + wetr * wet_amt;

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

    inline float GetVUSample() { return output_env_follower.GetLastSamp(); }

private:
    daisysp::DcBlock dcblock_line_in_l_, dcblock_line_in_r_;
    chompi::Limiter lim_hp_l_, lim_hp_r_, lim_line_l_, lim_line_r_;
    chompi::EnvFollower output_env_follower;
    float mgain_, mgain_target_;
    float ingain_, ingain_target_;
    float final_lim_, final_lim_target_;
    float mix_, mix_target_;
};
