/** @file FxChain.h
 *  @brief The punch-in effects in their processing order, with a level meter each for the
 *  key LEDs. The engine runs the chain on the summed signal, after the dry/wet mix and
 *  before the output gain and master compressor (passthroughEngine.h). FxSlots.h says which
 *  key and LEDs go with each effect, FxParams.h its knobs.
 *
 *  The order follows a pedalboard's: source, pitch, dirt, filter, modulation, gate, tape,
 *  space. The keys follow it too, left to right:
 *   freezer -> shifter -> folder -> crusher -> filter -> flanger -> slicer -> wow & flutter
 *              |<-------------- resonator loop --------------->|
 *     -> tape stop -> delay -> reverb
 *  The freezer comes first so it captures the clean sound and everything after it works on
 *  the repeats. The folder comes before the crusher, so it folds the clean signal and the
 *  crusher grinds the folds. The filter sweeps the dirt, and the flanger the harmonics it
 *  made. The slicer is the last insert, the final gate: it chops everything including the
 *  resonator's ringing, and the sends get the chopped sound. Wow & flutter and the tape stop
 *  come after it, so they bend everything before them; the sends after them, so a tape stop
 *  leaves the echoes and the reverb ringing. The delay's echoes feed the reverb as well as the
 *  output.
 *
 *  Three kinds:
 *   - insert: replaces the signal while its key is on; only the wet amount is faded. Page 2's
 *     Mix, Band and Level wrap it (FxOutput.h), except the tape stop's and the resonator's.
 *   - send (delay, reverb): the key fades what goes into the effect, and its return is added
 *     to the signal, so tails ring out after the key is released. Page 2's Band filters what
 *     goes in.
 *   - loop (resonator): a comb feedback loop from after the flanger back to after the
 *     freezer, so the filter is in the loop and the slicer outside it.
 */
#pragma once
#include "BenchProfile.h"
#include "EnvFollower.h"
#include "FxCrusher.h"
#include "FxDelay.h"
#include "FxFilter.h"
#include "FxFlanger.h"
#include "FxFolder.h"
#include "FxFreezer.h"
#include "FxOutput.h"
#include "FxResonator.h"
#include "FxReverb.h"
#include "FxShifter.h"
#include "FxSlicer.h"
#include "FxTapeStop.h"
#include "FxWarble.h"

namespace chompi
{

/** The effects, in the order of their keys, left to right (FxSlots.h) */
enum FxId
{
    FX_FREEZER,
    FX_SHIFTER,
    FX_FOLDER,
    FX_CRUSHER,
    FX_FILTER,
    FX_FLANGER,
    FX_RESONATOR,
    FX_SLICER,
    FX_WARBLE,
    FX_TAPESTOP,
    FX_DELAY,
    FX_REVERB,
    kNumFx,
};
#if FRIZZ_BENCH
static_assert(BenchProfile::COMP - BenchProfile::FX0 == kNumFx, "a bench part per effect");
#endif

/** Their names in the scene file (FxScenes.h): fixed, so saved scenes survive new effects
 *  and a new order */
static const char* const kFxNames[] = {
    "freezer", "shifter", "folder", "crusher", "filter",
    "flanger", "resonator", "slicer", "warble", "tapestop",
    "delay", "reverb",
};
static_assert(sizeof(kFxNames) / sizeof(kFxNames[0]) == kNumFx, "one per FxId");

// How long a scene recall's fast slew lasts, 50ms at 48kHz: 10 of its time constants
// (FxCommon.h), well past where it has settled
static const uint32_t kFxRecallSlewSamples = 2400;

// Into the meters' EnvFollowers, which add 5x: full brightness at about 1 (L+R)/2
static const float kFxMeterScale = .1f;

class FxChain
{
public:
    /** The delay's, the reverb's, the freezer's and the tape stop's buffers are statics in
     *  chompi_main.cpp: SDRAM for the delay, freezer and tape stop, DTCMRAM for the reverb */
    void Init(float sample_rate,
              float* delay_mem, size_t delay_frames,
              daisysp::Reverb* reverb,
              float* freezer_mem_l, float* freezer_mem_r, size_t freezer_frames,
              float* tapestop_mem_l, float* tapestop_mem_r, size_t tapestop_frames)
    {
        filter_.Init(sample_rate);
        crusher_.Init(sample_rate);
        folder_.Init(sample_rate);
        delay_.Init(delay_mem, delay_frames);
        reverb_.Init(sample_rate, reverb);
        freezer_.Init(sample_rate, freezer_mem_l, freezer_mem_r, freezer_frames);
        slicer_.Init(sample_rate);
        flanger_.Init(sample_rate);
        shifter_.Init(sample_rate);
        resonator_.Init(sample_rate);
        warble_.Init(sample_rate);
        tapestop_.Init(sample_rate, tapestop_mem_l, tapestop_mem_r, tapestop_frames);

        fx_[FX_FILTER] = &filter_;
        fx_[FX_CRUSHER] = &crusher_;
        fx_[FX_FOLDER] = &folder_;
        fx_[FX_FREEZER] = &freezer_;
        fx_[FX_SLICER] = &slicer_;
        fx_[FX_FLANGER] = &flanger_;
        fx_[FX_SHIFTER] = &shifter_;
        fx_[FX_RESONATOR] = &resonator_;
        fx_[FX_WARBLE] = &warble_;
        fx_[FX_TAPESTOP] = &tapestop_;
        fx_[FX_DELAY] = &delay_;
        fx_[FX_REVERB] = &reverb_;

        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            meter_[fx].Init();
            out_[fx].Init(sample_rate);
        }
        fast_slew_left_ = 0;
    }

    /** Once per block: the tempo and the pulses' real spacing in samples (TempoClock), then
     *  one call per clock pulse in the block with the clock's position (TempoClock::Pulse) */
    void SetTempo(float bpm, float pulse_samples)
    {
        delay_.SetTempo(bpm);
        filter_.SetPulseSamples(pulse_samples);
        freezer_.SetTempo(bpm);
        tapestop_.SetTempo(bpm);
    }
    /** reverse: the position counts down, a loop playing backwards (TempoClock.h) */
    void ClockPulse(uint32_t pos, bool reverse = false)
    {
        delay_.ClockPulse(pos);
        filter_.ClockPulse(pos, reverse);
        freezer_.ClockPulse(pos);
        slicer_.ClockPulse(pos);
    }

    /** One sample through the chain. The meters follow each insert's output and each send's
     *  return, so the send keys show the tails. */
    void Process(float* l, float* r)
    {
        if (fast_slew_left_ > 0 && --fast_slew_left_ == 0)
            FxSlew::coeff = kFxParamCoeff;

        out_[FX_FREEZER].Process(freezer_, l, r);
        if (!freezer_.Idle())
            Meter(FX_FREEZER, *l + *r);
        BENCH_MARK_FX(FX_FREEZER);
        // the resonator's loop wraps everything from here to the flanger
        resonator_.Feed(l, r);
        if (!resonator_.Idle())
            Meter(FX_RESONATOR, resonator_.Return());
        BENCH_MARK_FX(FX_RESONATOR);
        out_[FX_SHIFTER].Process(shifter_, l, r);
        if (!shifter_.Idle())
            Meter(FX_SHIFTER, *l + *r);
        BENCH_MARK_FX(FX_SHIFTER);
        out_[FX_FOLDER].Process(folder_, l, r);
        if (!folder_.Idle())
            Meter(FX_FOLDER, *l + *r);
        BENCH_MARK_FX(FX_FOLDER);
        out_[FX_CRUSHER].Process(crusher_, l, r);
        if (!crusher_.Idle())
            Meter(FX_CRUSHER, *l + *r);
        BENCH_MARK_FX(FX_CRUSHER);
        out_[FX_FILTER].Process(filter_, l, r);
        if (!filter_.Idle())
            Meter(FX_FILTER, *l + *r);
        BENCH_MARK_FX(FX_FILTER);
        out_[FX_FLANGER].Process(flanger_, l, r);
        if (!flanger_.Idle())
            Meter(FX_FLANGER, *l + *r);
        BENCH_MARK_FX(FX_FLANGER);
        resonator_.Tap(*l, *r);
        BENCH_MARK_FX(FX_RESONATOR);
        out_[FX_SLICER].Process(slicer_, l, r);
        if (!slicer_.Idle())
            Meter(FX_SLICER, *l + *r);
        BENCH_MARK_FX(FX_SLICER);
        out_[FX_WARBLE].Process(warble_, l, r);
        if (!warble_.Idle())
            Meter(FX_WARBLE, *l + *r);
        BENCH_MARK_FX(FX_WARBLE);
        tapestop_.Process(l, r);
        if (!tapestop_.Idle())
            Meter(FX_TAPESTOP, *l + *r);
        BENCH_MARK_FX(FX_TAPESTOP);

        // sends: the delay from the inserts' output, the reverb from that plus the delay's
        // return, so the echoes are reverberated. Both returns are added on top.
        const float sendl = *l, sendr = *r;
        float inl = sendl, inr = sendr;
        if (!delay_.Idle()) // what goes in is faded with the key
            out_[FX_DELAY].Band(&inl, &inr);
        delay_.Process(inl, inr, l, r);
        const float delayl = *l, delayr = *r;
        if (!delay_.Sleeping())
            Meter(FX_DELAY, delayl - sendl + delayr - sendr);
        BENCH_MARK_FX(FX_DELAY);
        inl = delayl;
        inr = delayr;
        if (!reverb_.Idle())
            out_[FX_REVERB].Band(&inl, &inr);
        reverb_.Process(inl, inr, l, r);
        if (!reverb_.Sleeping())
            Meter(FX_REVERB, *l - delayl + *r - delayr);
        BENCH_MARK_FX(FX_REVERB);
    }

    /** From the UI or the morph */
    inline void SetOn(size_t fx, bool on) { fx_[fx]->SetOn(on); }
    __attribute__((noinline)) void SetParam(size_t fx, size_t param, float val)
    {
        // page 2's shared knobs go to the effect's FxOutput, where it has one
        if (!(Shared(fx) && out_[fx].SetParam(param, val)))
            fx_[fx]->SetParam(param, val);
    }
    /** Whether fx has page 2's shared knobs (FxOutput.h): the sends only Band, which the
     *  FxOutput takes as well */
    static inline bool Shared(size_t fx) { return fx != FX_RESONATOR && fx != FX_TAPESTOP; }
    /** Before a scene recall's SetParams: the knobs slew at kFxRecallCoeff for
     *  kFxRecallSlewSamples, so the new scene lands at once */
    void FastSlew()
    {
        FxSlew::coeff = kFxRecallCoeff;
        fast_slew_left_ = kFxRecallSlewSamples;
    }
    /** 0..1, for the key LEDs */
    inline float GetLevel(size_t fx) { return meter_[fx].GetLastSamp(); }

    /** Off and doing no more than it does off: an insert faded out, a send whose tail has gone
     *  quiet (TailWatch). For the CPU bench (Bench.h), which reports what was still working */
    __attribute__((noinline)) bool Resting(size_t fx) const // the bench calls it in many places
    {
        if (fx == FX_DELAY)
            return delay_.Sleeping();
        if (fx == FX_REVERB)
            return reverb_.Sleeping();
        if (fx == FX_TAPESTOP)
            return tapestop_.Resting();
        return fx_[fx]->Idle();
    }

private:
    inline void Meter(size_t fx, float sum) { meter_[fx].Process(sum * kFxMeterScale); }

    Filter filter_;
    Crusher crusher_;
    Folder folder_;
    Freezer freezer_;
    Slicer slicer_;
    Flanger flanger_;
    Shifter shifter_;
    Resonator resonator_;
    Warble warble_;
    TapeStop tapestop_;
    DelaySend delay_;
    ReverbSend reverb_;
    FxBase* fx_[kNumFx];
    FxOutput out_[kNumFx]; // page 2's Mix, Band and Level (FxOutput.h), where Shared
    EnvFollower meter_[kNumFx];
    uint32_t fast_slew_left_; // samples of FastSlew to go
};

} // namespace chompi
