/** @file passthroughEngine.h
 *  @brief Audio engine: the stereo AUX input goes to both outputs through the Volume
 *  Engine: input gain -> dry/wet mix -> punch-in FX -> output gain -> master compressor.
 *  The punch-in FX are the freezer, slicer, flanger, shifter, filter and crusher (inserts, in
 *  that order), then the delay and reverb sends in parallel.
 *
 *  Dry is the input on its own, wet is the looper's playback on its own. The looper
 *  records the dry signal (see Looper.h).
 *
 *  The input level, output level and compressor stage are ported from TAPE's DSPEngine
 *  so the gains match the hardware the way TAPE tuned them.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "EnvFollower.h"
#include "FxWizard.h"
#include "limiter.h"
#include "Looper.h"
#include "PunchFx.h"
#include "TempoClock.h"

using namespace daisy;
using namespace daisysp;

static constexpr float kLineOutGain = .3f;
static constexpr float kHpGain = .2f;
static constexpr float kLineInGain = 3.f;
// Into the FX meters' EnvFollowers, which add 5x: full brightness at about 1 (L+R)/2
static constexpr float kFxMeterScale = .1f;

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
        filter_.Init(sample_rate);
        crusher_.Init(sample_rate);
        delay_.Init(delay_mem, delay_frozen_mem, delay_frames);
        reverb_.Init(sample_rate, reverb);
        freezer_.Init(sample_rate, freezer_mem_l, freezer_mem_r, freezer_frames);
        slicer_.Init(sample_rate);
        flanger_.Init(sample_rate);
        shifter_.Init(sample_rate);

        dcblock_line_in_l_.Init(sample_rate);
        dcblock_line_in_r_.Init(sample_rate);

        lim_hp_l_.Init();
        lim_hp_r_.Init();
        lim_line_l_.Init();
        lim_line_r_.Init();

        output_env_follower.Init();
        for (size_t fx = 0; fx < chompi::kNumFx; fx++)
            fx_env_[fx].Init();
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

        // the tempo and clock of the delay, the filter LFO, the freezer and the slicer, once
        // per block
        const uint32_t pulses = tempo_clock_.Process(size);
        delay_.SetTempo(tempo_clock_.GetTempo());
        filter_.SetTempo(tempo_clock_.GetTempo());
        freezer_.SetTempo(tempo_clock_.GetTempo());
        for (uint32_t p = 0; p < pulses; p++)
        {
            delay_.ClockPulse(tempo_clock_.Pulse());
            filter_.ClockPulse();
            freezer_.ClockPulse();
            slicer_.ClockPulse();
        }

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
            freezer_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_FREEZER].Process((sigl + sigr) * kFxMeterScale);
            slicer_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_SLICER].Process((sigl + sigr) * kFxMeterScale);
            flanger_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_FLANGER].Process((sigl + sigr) * kFxMeterScale);
            shifter_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_SHIFTER].Process((sigl + sigr) * kFxMeterScale);
            filter_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_FILTER].Process((sigl + sigr) * kFxMeterScale);
            crusher_.Process(&sigl, &sigr);
            fx_env_[chompi::FX_CRUSHER].Process((sigl + sigr) * kFxMeterScale);

            // sends, in parallel from the crusher's output, their returns added on top; the
            // meters follow the returns, so they show the tails
            const float sendl = sigl, sendr = sigr;
            delay_.Process(sendl, sendr, &sigl, &sigr);
            const float delayl = sigl, delayr = sigr;
            fx_env_[chompi::FX_DELAY].Process((delayl - sendl + delayr - sendr) * kFxMeterScale);
            reverb_.Process(sendl, sendr, &sigl, &sigr);
            fx_env_[chompi::FX_REVERB].Process((sigl - delayl + sigr - delayr) * kFxMeterScale);

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

    /** Punch-in FX, by FxId (see PunchFx.h) */
    inline void SetFxOn(size_t fx, bool on)
    {
        switch (fx)
        {
        case chompi::FX_FILTER:  filter_.SetOn(on); break;
        case chompi::FX_CRUSHER: crusher_.SetOn(on); break;
        case chompi::FX_DELAY:   delay_.SetOn(on); break;
        case chompi::FX_REVERB:  reverb_.SetOn(on); break;
        case chompi::FX_FREEZER: freezer_.SetOn(on); break;
        case chompi::FX_SLICER:  slicer_.SetOn(on); break;
        case chompi::FX_FLANGER: flanger_.SetOn(on); break;
        case chompi::FX_SHIFTER: shifter_.SetOn(on); break;
        default: break;
        }
    }
    inline void SetFxParam(size_t fx, size_t param, float val)
    {
        switch (fx)
        {
        case chompi::FX_FILTER:  filter_.SetParam(param, val); break;
        case chompi::FX_CRUSHER: crusher_.SetParam(param, val); break;
        case chompi::FX_DELAY:   delay_.SetParam(param, val); break;
        case chompi::FX_REVERB:  reverb_.SetParam(param, val); break;
        case chompi::FX_FREEZER: freezer_.SetParam(param, val); break;
        case chompi::FX_SLICER:  slicer_.SetParam(param, val); break;
        case chompi::FX_FLANGER: flanger_.SetParam(param, val); break;
        case chompi::FX_SHIFTER: shifter_.SetParam(param, val); break;
        default: break;
        }
    }

    inline float GetVUSample() { return output_env_follower.GetLastSamp(); }
    /** 0..1, for the FX key LEDs: an insert's output, a send's return */
    inline float GetFxLevel(size_t fx) { return fx_env_[fx].GetLastSamp(); }

    chompi::Looper looper;

private:
    daisysp::DcBlock dcblock_line_in_l_, dcblock_line_in_r_;
    chompi::Limiter lim_hp_l_, lim_hp_r_, lim_line_l_, lim_line_r_;
    chompi::EnvFollower output_env_follower;
    chompi::EnvFollower fx_env_[chompi::kNumFx];
    chompi::TempoClock tempo_clock_;
    chompi::Filter filter_;
    chompi::Crusher crusher_;
    chompi::DelaySend delay_;
    chompi::ReverbSend reverb_;
    chompi::Freezer freezer_;
    chompi::Slicer slicer_;
    chompi::Flanger flanger_;
    chompi::Shifter shifter_;
    float mgain_, mgain_target_;
    float ingain_, ingain_target_;
    float final_lim_, final_lim_target_;
    float mix_, mix_target_;
};
