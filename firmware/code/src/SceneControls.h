/** @file SceneControls.h
 *  @brief What the play page does with the FX scene keys, without the hardware: recalling a
 *  slot or morphing to it (SHIFT, FxMorph.h), and TAPE's and TEMPO's SAVE / COPY / DELETE flow (pick the mode, the slot(s), then
 *  confirm). Like a mode key, a slot tapped again is deselected. NormalPage.h routes the
 *  keys here, writes the card (SceneStore.h) and draws the LEDs; test/controls.cpp runs it on
 *  the host.
 *
 *  The first slot (kBlankSlot) is the blank scene: every effect off, every knob on its
 *  default. It can be recalled and copied from, but not saved over, copied over or deleted.
 */
#pragma once
#include "FxControls.h"

namespace chompi
{

enum class SceneMode
{
    NONE,
    SAVE,
    COPY,
    DELETE,
};

static const int kNoScene = -1;

template <class Engine>
class SceneControls
{
public:
    /** What pressing a slot did */
    enum class Slot
    {
        SELECTED,   // picked for the mode or deselected, or nothing to do
        RECALL,     // recall it: Recall(slot), with the audio interrupt blocked on the device
        MORPH,      // morph to it: Morph(slot), likewise
        MORPH_MORE, // the slot being morphed to: MorphMore(), likewise
        REFUSED,    // a slot the mode can't act on: empty where a saved one is needed, or
                    // blank; or SHIFT + another slot than the one being morphed to
    };

    void Init(FxScene* scenes, FxControls<Engine>* fx)
    {
        scenes_ = scenes;
        fx_ = fx;

        FxScene& blank = scenes_[kBlankSlot];
        blank.used = true;
        blank.latched = 0;
        for (size_t f = 0; f < kNumFx; f++)
            for (size_t p = 0; p < kNumFxParams; p++)
                blank.params[f][p] = kFxParams[f].defaults[p];

        mode_ = SceneMode::NONE;
        sel_ = src_ = active_ = morph_ = kNoScene;
    }

    /** A mode key: the same one again cancels, another switches over */
    void ModePressed(SceneMode mode)
    {
        mode_ = mode_ == mode ? SceneMode::NONE : mode;
        sel_ = src_ = kNoScene;
    }

    /** shift: SHIFT held, a morph rather than a recall; while one runs, only to the slot it
     *  runs to. In a mode, SHIFT makes no difference */
    Slot SlotPressed(size_t slot, bool shift = false)
    {
        const int s = static_cast<int>(slot);
        if (mode_ == SceneMode::NONE)
        {
            if (shift && fx_->Morphing())
                return s == morph_ ? Slot::MORPH_MORE : Slot::REFUSED;
            if (!scenes_[slot].used)
                return Slot::REFUSED;
            return shift ? Slot::MORPH : Slot::RECALL;
        }

        // COPY's source again: back to picking the source, once no destination is picked
        if (mode_ == SceneMode::COPY && s == src_)
        {
            if (sel_ == kNoScene)
                src_ = kNoScene;
            return Slot::SELECTED;
        }
        if (!Valid(slot))
            return Slot::REFUSED;

        if (mode_ == SceneMode::COPY && src_ == kNoScene)
            src_ = s; // the source first, then the destination
        else
            sel_ = s == sel_ ? kNoScene : s;
        return Slot::SELECTED;
    }

    /** Recalls a used slot, which becomes the active scene */
    void Recall(size_t slot)
    {
        fx_->Recall(scenes_[slot]);
        active_ = static_cast<int>(slot);
    }

    /** Morphs to a used slot, which becomes the active scene now */
    void Morph(size_t slot)
    {
        fx_->Morph(scenes_[slot]);
        active_ = morph_ = static_cast<int>(slot);
    }

    /** One bar line more for the running morph; false if it can't */
    inline bool MorphMore() { return fx_->MorphMore(); }

    /** Stops the morph where it is (SHIFT + PLAY): its scene stays active, edited. False if
     *  none runs */
    inline bool FreezeMorph() { return fx_->FreezeMorph(); }

    /** The slot a morph runs to, kNoScene if none runs */
    inline int Morphing() const { return fx_->Morphing() ? morph_ : kNoScene; }

    /** Whether pressing the slot would pick it in the current mode: SAVE any slot but the
     *  blank one, DELETE a saved one, COPY's source a saved one or the blank one, COPY's
     *  destination any but the source and the blank one. False without a mode */
    bool Valid(size_t slot) const
    {
        const bool used = scenes_[slot].used;
        const bool blank = slot == kBlankSlot;
        switch (mode_)
        {
        case SceneMode::SAVE:
            return !blank;
        case SceneMode::COPY:
            return src_ == kNoScene ? used : static_cast<int>(slot) != src_ && !blank;
        case SceneMode::DELETE:
            return used && !blank;
        case SceneMode::NONE:
            break;
        }
        return false;
    }

    /** True while the CHOMPI key would confirm: a mode with its slot picked */
    inline bool Armed() const { return mode_ != SceneMode::NONE && sel_ != kNoScene; }

    /** The CHOMPI key: carries out the armed mode on its slot and ends the mode. Returns the
     *  slot it changed, to be written to the card, or kNoScene if it wasn't armed */
    int Confirm()
    {
        if (!Armed())
            return kNoScene;

        FxScene& target = scenes_[sel_];
        switch (mode_)
        {
        case SceneMode::SAVE:
            fx_->Snapshot(target);
            active_ = sel_;
            break;
        case SceneMode::COPY:
            target = scenes_[src_];
            // the sound stays, so it no longer matches the active scene
            if (active_ == sel_)
                fx_->MarkEdited();
            break;
        case SceneMode::DELETE:
            target.used = false;
            if (active_ == sel_)
                active_ = kNoScene;
            break;
        case SceneMode::NONE:
            break;
        }

        const int changed = sel_;
        mode_ = SceneMode::NONE;
        sel_ = src_ = kNoScene;
        return changed;
    }

    inline SceneMode Mode() const { return mode_; }
    /** The slot the mode acts on, kNoScene until picked */
    inline int Selected() const { return sel_; }
    /** COPY's source, kNoScene until picked */
    inline int Source() const { return src_; }
    /** The last scene recalled or saved, kNoScene if none or deleted */
    inline int Active() const { return active_; }
    /** The controls changed since the active scene was recalled or saved */
    inline bool Edited() const { return fx_->Edited(); }

private:
    FxScene* scenes_ = nullptr;
    FxControls<Engine>* fx_ = nullptr;
    SceneMode mode_ = SceneMode::NONE;
    int sel_ = kNoScene;
    int src_ = kNoScene;
    int active_ = kNoScene;
    int morph_ = kNoScene; // the slot the last morph ran to
};

} // namespace chompi
