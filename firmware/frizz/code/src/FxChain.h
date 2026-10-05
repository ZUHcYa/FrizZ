/** @file FxChain.h
 *  @brief The punch-in effects in their processing order, with a level meter each for the
 *  key LEDs. The engine runs the chain on the summed signal, after the dry/wet mix and
 *  before the output gain and master compressor (passthroughEngine.h). FxSlots.h says which
 *  key, LED and knobs go with each effect.
 *
 *  Three kinds:
 *   - insert: replaces the signal while its key is on; only the wet amount is faded.
 *   - send (delay, reverb): the key fades what goes into the effect, and its return is added
 *     to the signal, so tails ring out after the key is released.
 *   - loop (resonator): a comb feedback loop around a stretch of the inserts.
 */
#pragma once
#include "EnvFollower.h"
#include "FxCrusher.h"
#include "FxDelay.h"
#include "FxFilter.h"
#include "FxFlanger.h"
#include "FxFreezer.h"
#include "FxResonator.h"
#include "FxReverb.h"
#include "FxShifter.h"
#include "FxSlicer.h"

namespace chompi
{

/** The effects, in the order of their keys, left to right (FxSlots.h) */
enum FxId
{
    FX_FILTER,
    FX_CRUSHER,
    FX_FREEZER,
    FX_SLICER,
    FX_FLANGER,
    FX_SHIFTER,
    FX_RESONATOR,
    FX_DELAY,
    FX_REVERB,
    kNumFx,
};

// Into the meters' EnvFollowers, which add 5x: full brightness at about 1 (L+R)/2
static const float kFxMeterScale = .1f;

class FxChain
{
public:
    /** The delay's, the reverb's and the freezer's buffers are statics in chompi_main.cpp:
     *  SDRAM for the delay and freezer, DTCMRAM for the reverb */
    void Init(float sample_rate,
              float* delay_mem, float* delay_frozen_mem, size_t delay_frames,
              daisysp::Reverb* reverb,
              float* freezer_mem_l, float* freezer_mem_r, size_t freezer_frames)
    {
        filter_.Init(sample_rate);
        crusher_.Init(sample_rate);
        delay_.Init(delay_mem, delay_frozen_mem, delay_frames);
        reverb_.Init(sample_rate, reverb);
        freezer_.Init(sample_rate, freezer_mem_l, freezer_mem_r, freezer_frames);
        slicer_.Init(sample_rate);
        flanger_.Init(sample_rate);
        shifter_.Init(sample_rate);
        resonator_.Init(sample_rate);

        fx_[FX_FILTER] = &filter_;
        fx_[FX_CRUSHER] = &crusher_;
        fx_[FX_FREEZER] = &freezer_;
        fx_[FX_SLICER] = &slicer_;
        fx_[FX_FLANGER] = &flanger_;
        fx_[FX_SHIFTER] = &shifter_;
        fx_[FX_RESONATOR] = &resonator_;
        fx_[FX_DELAY] = &delay_;
        fx_[FX_REVERB] = &reverb_;

        for (size_t fx = 0; fx < kNumFx; fx++)
            meter_[fx].Init();
    }

    /** Once per block: the tempo, then one call per clock pulse in the block with the
     *  clock's position (TempoClock::Pulse) */
    void SetTempo(int bpm)
    {
        delay_.SetTempo(bpm);
        filter_.SetTempo(bpm);
        freezer_.SetTempo(bpm);
    }
    void ClockPulse(uint32_t pos)
    {
        delay_.ClockPulse(pos);
        filter_.ClockPulse(pos);
        freezer_.ClockPulse(pos);
        slicer_.ClockPulse(pos);
    }

    /** One sample through the chain. The meters follow each insert's output and each send's
     *  return, so the send keys show the tails. */
    void Process(float* l, float* r)
    {
        freezer_.Process(l, r);
        Meter(FX_FREEZER, *l + *r);
        // the resonator's loop wraps everything from here to the crusher
        resonator_.Feed(l, r);
        Meter(FX_RESONATOR, resonator_.Return());
        slicer_.Process(l, r);
        Meter(FX_SLICER, *l + *r);
        flanger_.Process(l, r);
        Meter(FX_FLANGER, *l + *r);
        shifter_.Process(l, r);
        Meter(FX_SHIFTER, *l + *r);
        filter_.Process(l, r);
        Meter(FX_FILTER, *l + *r);
        crusher_.Process(l, r);
        Meter(FX_CRUSHER, *l + *r);
        resonator_.Tap(*l, *r);

        // sends, in parallel from the crusher's output, their returns added on top
        const float sendl = *l, sendr = *r;
        delay_.Process(sendl, sendr, l, r);
        const float delayl = *l, delayr = *r;
        Meter(FX_DELAY, delayl - sendl + delayr - sendr);
        reverb_.Process(sendl, sendr, l, r);
        Meter(FX_REVERB, *l - delayl + *r - delayr);
    }

    inline void SetOn(size_t fx, bool on) { fx_[fx]->SetOn(on); }
    inline void SetParam(size_t fx, size_t param, float val) { fx_[fx]->SetParam(param, val); }
    /** 0..1, for the key LEDs */
    inline float GetLevel(size_t fx) { return meter_[fx].GetLastSamp(); }

private:
    inline void Meter(size_t fx, float sum) { meter_[fx].Process(sum * kFxMeterScale); }

    Filter filter_;
    Crusher crusher_;
    Freezer freezer_;
    Slicer slicer_;
    Flanger flanger_;
    Shifter shifter_;
    Resonator resonator_;
    DelaySend delay_;
    ReverbSend reverb_;
    FxBase* fx_[kNumFx];
    EnvFollower meter_[kNumFx];
};

} // namespace chompi
