/** @file SceneControls.h
 *  @brief What the play page does with the FX scene keys, without the hardware: recalling a
 *  slot, and TAPE's and TEMPO's SAVE / COPY / DELETE flow (pick the mode, the slot(s), then
 *  confirm). NormalPage.h routes the keys here, writes the card (SceneStore.h) and draws the
 *  LEDs; test/controls.cpp runs it on the host.
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
        SELECTED, // picked for the mode, or nothing to do
        RECALL,   // recall it: Recall(slot), with the audio interrupt blocked on the device
        EMPTY,    // an empty slot where a saved one is needed
    };

    void Init(FxScene* scenes, FxControls<Engine>* fx)
    {
        scenes_ = scenes;
        fx_ = fx;
        mode_ = SceneMode::NONE;
        sel_ = src_ = active_ = kNoScene;
    }

    /** A mode key: the same one again cancels, another switches over */
    void ModePressed(SceneMode mode)
    {
        mode_ = mode_ == mode ? SceneMode::NONE : mode;
        sel_ = src_ = kNoScene;
    }

    Slot SlotPressed(size_t slot)
    {
        const bool used = scenes_[slot].used;
        const int s = static_cast<int>(slot);
        switch (mode_)
        {
        case SceneMode::NONE:
            return used ? Slot::RECALL : Slot::EMPTY;
        case SceneMode::SAVE:
            sel_ = s;
            break;
        case SceneMode::COPY:
            // the source first, then the destination
            if (src_ == kNoScene)
            {
                if (!used)
                    return Slot::EMPTY;
                src_ = s;
            }
            else if (s != src_)
                sel_ = s;
            break;
        case SceneMode::DELETE:
            if (!used)
                return Slot::EMPTY;
            sel_ = s;
            break;
        }
        return Slot::SELECTED;
    }

    /** Recalls a used slot, which becomes the active scene */
    void Recall(size_t slot)
    {
        fx_->Recall(scenes_[slot]);
        active_ = static_cast<int>(slot);
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
};

} // namespace chompi
