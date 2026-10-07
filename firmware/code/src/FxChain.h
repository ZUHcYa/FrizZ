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
 *   - insert: replaces the signal while its key is on; only the wet amount is faded.
 *   - send (delay, reverb): the key fades what goes into the effect, and its return is added
 *     to the signal, so tails ring out after the key is released.
 *   - loop (resonator): a comb feedback loop from after the flanger back to after the
 *     freezer, so the filter is in the loop and the slicer outside it.
 *
 *  The randomizer (FxRandomizer.h) plays the effects in kRandomPool from outside the chain:
 *  while one of its gates has an effect, that effect is the randomizer's (owned_), on and
 *  with random knobs; the UI's SetOn and SetParam are kept and land once it's the user's
 *  again, after the gate has faded out. Pressing its key takes it back at once. Meanwhile
 *  a LevelGuard holds the inserts (after the freezer, up to the tape stop) to at most 3dB over
 *  what went into them; the sends come after it, so they only hear the held sound and the
 *  whole chain can't jump by more than that per gate. Since the chain is serial, the user's
 *  own inserts playing during a gate are turned down with it.
 */
#pragma once
#include <atomic>
#include "EnvFollower.h"
#include "FxCrusher.h"
#include "FxDelay.h"
#include "FxFilter.h"
#include "FxFlanger.h"
#include "FxFolder.h"
#include "FxFreezer.h"
#include "FxRandomizer.h"
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

/** Their names in the scene file (FxScenes.h): fixed, so saved scenes survive new effects
 *  and a new order */
static const char* const kFxNames[] = {
    "freezer", "shifter", "folder", "crusher", "filter",
    "flanger", "resonator", "slicer", "warble", "tapestop",
    "delay", "reverb",
};
static_assert(sizeof(kFxNames) / sizeof(kFxNames[0]) == kNumFx, "one per FxId");

// What the randomizer plays: every effect but the sends and the freezer. The freezer waits
// for the next 16th to start recording, and a gate is over by then: it wouldn't be heard
static const uint16_t kRandomPool = static_cast<uint16_t>(
    ((1u << kNumFx) - 1) & ~((1u << FX_FREEZER) | (1u << FX_DELAY) | (1u << FX_REVERB)));

// The most a random gate may make the inserts louder than what goes into them: +3dB
static const float kRandomHeadroom = 1.4125f;

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
            for (size_t p = 0; p < kNumFxParams; p++)
                user_params_[fx][p] = 0.f;
        }
        fast_slew_left_ = 0;
        randomizer_.Init(sample_rate, kRandomPool);
        level_.Init(sample_rate, kRandomHeadroom);
        user_on_ = owned_ = claimed_ = 0;
    }

    /** Once per block: the tempo, then one call per clock pulse in the block with the
     *  clock's position (TempoClock::Pulse) */
    void SetTempo(int bpm)
    {
        delay_.SetTempo(bpm);
        filter_.SetTempo(bpm);
        freezer_.SetTempo(bpm);
        tapestop_.SetTempo(bpm);
        randomizer_.SetTempo(bpm);
    }
    /** reverse: the position counts down, a loop playing backwards (TempoClock.h) */
    void ClockPulse(uint32_t pos, bool reverse = false)
    {
        delay_.ClockPulse(pos);
        filter_.ClockPulse(pos, reverse);
        freezer_.ClockPulse(pos);
        slicer_.ClockPulse(pos);
        randomizer_.ClockPulse(pos);
    }

    /** Once per block, after the pulses: the randomizer's gates take and give back their
     *  effects. Nothing while it's off and owns none */
    void RandomBlock(size_t size)
    {
        // keys pressed on an effect a gate had: the user's now
        const uint16_t claimed = claimed_.exchange(0);
        if (claimed)
        {
            for (size_t fx = 0; fx < kNumFx; fx++)
                if ((claimed >> fx) & 1 && Owned(fx))
                    GiveBack(fx, false);
        }
        if (randomizer_.Idle() && !owned_)
            return;

        const uint16_t before = randomizer_.Mask();
        randomizer_.Block(size, user_on_);
        const uint16_t mask = randomizer_.Mask();
        const uint16_t taken = randomizer_.Taken();
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            const uint16_t bit = Bit(fx);
            if (taken & bit)
            {
                // owned first, so a SetParam from the UI that this interrupts puts it back
                owned_ |= bit;
                for (size_t p = 0; p < kNumFxParams; p++)
                    fx_[fx]->SetParam(p, randomizer_.Value(fx, p));
                fx_[fx]->SnapParams();
                fx_[fx]->SetOn(true);
            }
            else if ((before & bit) && !(mask & bit))
                fx_[fx]->SetOn(false); // the gate closed: it fades out
            else if (Owned(fx) && !(mask & bit) && !randomizer_.Cooling(fx))
                GiveBack(fx, true); // faded out: the user's knobs, silently
        }
    }

    /** One sample through the chain. The meters follow each insert's output and each send's
     *  return, so the send keys show the tails. */
    void Process(float* l, float* r)
    {
        if (fast_slew_left_ > 0 && --fast_slew_left_ == 0)
            FxSlew::coeff = kFxParamCoeff;

        freezer_.Process(l, r);
        Meter(FX_FREEZER, *l + *r);
        const float in_l = *l, in_r = *r; // the randomizer's level guard's reference
        // the resonator's loop wraps everything from here to the flanger
        resonator_.Feed(l, r);
        Meter(FX_RESONATOR, resonator_.Return());
        shifter_.Process(l, r);
        Meter(FX_SHIFTER, *l + *r);
        folder_.Process(l, r);
        Meter(FX_FOLDER, *l + *r);
        crusher_.Process(l, r);
        Meter(FX_CRUSHER, *l + *r);
        filter_.Process(l, r);
        Meter(FX_FILTER, *l + *r);
        flanger_.Process(l, r);
        Meter(FX_FLANGER, *l + *r);
        resonator_.Tap(*l, *r);
        slicer_.Process(l, r);
        Meter(FX_SLICER, *l + *r);
        warble_.Process(l, r);
        Meter(FX_WARBLE, *l + *r);
        tapestop_.Process(l, r);
        Meter(FX_TAPESTOP, *l + *r);
        level_.Process(in_l, in_r, l, r, owned_ != 0);

        // sends: the delay from the inserts' output, the reverb from that plus the delay's
        // return, so the echoes are reverberated. Both returns are added on top.
        const float sendl = *l, sendr = *r;
        delay_.Process(sendl, sendr, l, r);
        const float delayl = *l, delayr = *r;
        Meter(FX_DELAY, delayl - sendl + delayr - sendr);
        reverb_.Process(delayl, delayr, l, r);
        Meter(FX_REVERB, *l - delayl + *r - delayr);
    }

    /** From the UI or the morph. An effect the randomizer has takes it when it's the user's
     *  again; a key coming on takes it back (claimed_), in the next RandomBlock */
    void SetOn(size_t fx, bool on)
    {
        const uint16_t bit = Bit(fx);
        // atomic: the UI's and the morph's (in the audio callback) may meet here
        if (on)
            user_on_.fetch_or(bit);
        else
            user_on_.fetch_and(static_cast<uint16_t>(~bit));
        if (!Owned(fx))
            fx_[fx]->SetOn(on);
        else if (on)
            claimed_.fetch_or(bit);
    }
    void SetParam(size_t fx, size_t param, float val)
    {
        user_params_[fx][param] = val;
        if (Owned(fx))
            return;
        fx_[fx]->SetParam(param, val);
        // a gate taking it meanwhile (the audio interrupt): its value back
        if (Owned(fx))
            fx_[fx]->SetParam(param, randomizer_.Value(fx, param));
    }
    inline void SetRandomizerOn(bool on) { randomizer_.SetOn(on); }
    inline void SetRandomizerParam(size_t param, float val) { randomizer_.SetParam(param, val); }
    /** For the LEDs: the effects a random gate has on, the gates fired so far and the first
     *  effect the last one picked */
    inline uint16_t RandomMask() const { return randomizer_.Mask(); }
    inline uint32_t RandomFires() const { return randomizer_.Fires(); }
    inline size_t RandomPick() const { return randomizer_.LastPick(); }
    /** The randomizer has the effect: a gate's on, or it's fading out of one */
    inline bool Owned(size_t fx) const { return (owned_ >> fx) & 1; }
    /** Before a scene recall's SetParams: the knobs slew at kFxRecallCoeff for
     *  kFxRecallSlewSamples, so the new scene lands at once */
    void FastSlew()
    {
        FxSlew::coeff = kFxRecallCoeff;
        fast_slew_left_ = kFxRecallSlewSamples;
    }
    /** 0..1, for the key LEDs */
    inline float GetLevel(size_t fx) { return meter_[fx].GetLastSamp(); }

private:
    inline void Meter(size_t fx, float sum) { meter_[fx].Process(sum * kFxMeterScale); }
    static inline uint16_t Bit(size_t fx) { return static_cast<uint16_t>(1u << fx); }

    /** The effect is the user's again: their knobs and key. snap: it's silent, so the knobs
     *  jump; otherwise they slew, it's playing */
    void GiveBack(size_t fx, bool snap)
    {
        randomizer_.Drop(fx);
        owned_ &= static_cast<uint16_t>(~Bit(fx));
        for (size_t p = 0; p < kNumFxParams; p++)
            fx_[fx]->SetParam(p, user_params_[fx][p]);
        if (snap)
            fx_[fx]->SnapParams();
        fx_[fx]->SetOn((user_on_ >> fx) & 1);
    }

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
    EnvFollower meter_[kNumFx];
    uint32_t fast_slew_left_; // samples of FastSlew to go
    Randomizer randomizer_;
    LevelGuard level_; // holds the inserts to +3dB while the randomizer has any
    float user_params_[kNumFx][kNumFxParams]; // what the UI and the morph set last
    std::atomic<uint16_t> user_on_;           // bit fx: its key is on
    volatile uint16_t owned_;                 // bit fx: the randomizer has it (audio only)
    std::atomic<uint16_t> claimed_;           // bit fx: its key came on while owned
};

} // namespace chompi
