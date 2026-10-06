/** @file PlayKeys.h
 *  @brief What the play page does with the CHOMPI, PLAY and LOOP keys, without the hardware.
 *  NormalPage.h routes the keys here; test/keys.cpp runs it on the host.
 *
 *  CHOMPI held is SHIFT. In a scene mode with a slot picked (SceneArmed), a tap of it
 *  confirms: on its release, and only if nothing else was pressed or turned while it was
 *  held, so holding it still works as SHIFT there. SHIFT + an FX key latches (FxControls.h);
 *  SHIFT + anything else is that combo, and a latch the SHIFT press had queued for a held FX
 *  key is dropped (ShiftUsed).
 *
 *  LOOP acts on press, so recording starts and stops exactly then. PLAY acts on release, and
 *  only if nothing combined with it during the hold, so the PLAY + LOOP combos (the quantized
 *  record, the erase) never also toggle play. SHIFT + LOOP is tap tempo and only that;
 *  SHIFT + PLAY stops a scene morph, or without one is PLAY. A release whose press wasn't seen
 *  (a key held through the boot animation) does nothing.
 *
 *  Host is NormalPage on the device, a fake on the host. It needs: SceneArmed(),
 *  ConfirmScene(), ShiftPressed() and ShiftUsed() (FxControls'), FreezeMorph() (true if one was
 *  stopped), Tap(), Refused() (LOOP can't do it), Now() in ms, and the looper's
 *  LooperState(), CanRecordQuantized(), StartRecording(quantized), StopRecording(),
 *  TogglePlay().
 */
#pragma once
#include <stdint.h>
#include "Looper.h"

namespace chompi
{

template <class Host>
class PlayKeys
{
public:
    void Init(Host* host) { host_ = host; }

    inline bool Shift() const { return chompi_down_; }
    /** CHOMPI is held and has been used as SHIFT, so its release won't confirm */
    inline bool ShiftCombo() const { return chompi_down_ && combo_; }

    void Chompi(bool down)
    {
        if (down)
        {
            chompi_down_ = true;
            combo_ = false;
            host_->ShiftPressed();
            return;
        }
        if (!chompi_down_)
            return;
        chompi_down_ = false;
        if (!combo_ && host_->SceneArmed())
        {
            host_->ShiftUsed(); // a confirm, not SHIFT for the FX keys held
            host_->ConfirmScene();
        }
    }

    /** Any other key or knob going down or turning, but an FX key: with CHOMPI held, that's a
     *  SHIFT combo */
    void Used()
    {
        if (!chompi_down_)
            return;
        combo_ = true;
        host_->ShiftUsed();
    }

    /** An FX key going down: with CHOMPI held, a latch, so CHOMPI won't confirm */
    void FxKey()
    {
        if (chompi_down_)
            combo_ = true;
    }

    void Play(bool down)
    {
        if (down)
        {
            Used();
            play_down_ = true;
            play_combo_ = false;
            // SHIFT + PLAY: stops a morph, and only that; without one, PLAY as ever
            if (Shift() && host_->FreezeMorph())
            {
                play_combo_ = true;
                return;
            }
            // LOOP held first, then PLAY: the same erase combo
            if (loop_down_ && LoopExists())
            {
                play_combo_ = true;
                ArmErase();
            }
            return;
        }
        if (!play_down_)
            return;
        play_down_ = false;
        erase_armed_ = false;
        if (!play_combo_)
            host_->TogglePlay();
    }

    void Loop(bool down)
    {
        if (down)
        {
            Used();
            // SHIFT + LOOP: tap tempo; it doesn't count as LOOP held for PLAY's combos, nor
            // lets a held PLAY toggle on release
            if (Shift())
            {
                if (play_down_)
                    play_combo_ = true;
                host_->Tap();
                return;
            }
            loop_down_ = true;
            LoopPressed();
            return;
        }
        if (!loop_down_)
            return;
        loop_down_ = false;
        erase_armed_ = false;
    }

    /** PLAY + LOOP held long enough on a loop: true once, then erase it */
    bool EraseDue(uint32_t now)
    {
        if (!erase_armed_ || now - erase_time_ < kEraseHoldMs)
            return false;
        erase_armed_ = false;
        return true;
    }

    static const uint32_t kEraseHoldMs = 2000;

private:
    void LoopPressed()
    {
        if (play_down_)
            play_combo_ = true; // PLAY's release must not toggle

        switch (host_->LooperState())
        {
        case Looper::State::EMPTY:
            if (!play_down_)
                host_->StartRecording(false);
            else if (host_->CanRecordQuantized())
                host_->StartRecording(true);
            else
                host_->Refused();
            break;

        case Looper::State::RECORDING:
            host_->StopRecording();
            break;

        case Looper::State::PLAYING:
        case Looper::State::PAUSED:
            // only a loop that already existed when both went down can be erased, so holding
            // the quantized-record combo can't erase the new recording. Either key may go
            // down first.
            if (play_down_)
                ArmErase();
            break;
        }
    }

    bool LoopExists() const
    {
        const Looper::State state = host_->LooperState();
        return state == Looper::State::PLAYING || state == Looper::State::PAUSED;
    }

    void ArmErase()
    {
        erase_armed_ = true;
        erase_time_ = host_->Now();
    }

    Host* host_ = nullptr;
    bool chompi_down_ = false;
    bool combo_ = false;      // something used with CHOMPI during this hold
    bool play_down_ = false;
    bool loop_down_ = false;
    bool play_combo_ = false; // something combined with this PLAY hold: no toggle
    bool erase_armed_ = false;
    uint32_t erase_time_ = 0;
};

} // namespace chompi
