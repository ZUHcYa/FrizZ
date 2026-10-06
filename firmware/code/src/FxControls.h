/** @file FxControls.h
 *  @brief What the play page does with the punch-in FX keys and knobs 1-4, without the
 *  hardware: the parameters, which keys are held and latched, which FX the knobs edit, fine,
 *  stepped and coarse turns, and taking or recalling a scene. NormalPage.h routes the keys and
 *  knobs here and draws the LEDs from it; test/controls.cpp runs it on the host.
 *
 *  Engine is PassthroughEngine on the device, a fake on the host. It needs SetFxOn(fx, on),
 *  SetFxParam(fx, param, val) and FastFxSlew().
 */
#pragma once
#include <math.h>
#include "FxParams.h"
#include "FxScenes.h"

namespace chompi
{

// Knobs: 1% per detent; a stepped parameter moves one step per kFxDetentsPerStep detents
static const float kFxParamStep = .01f;
static const float kFxDetentsPerStep = 3.f;

template <class Engine>
class FxControls
{
public:
    /** Every FX off, every parameter on its default, pushed to the engine */
    void Init(Engine* engine)
    {
        engine_ = engine;
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            held_[fx] = latched_[fx] = false;
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                params_[fx][p] = -1.f; // so every default counts as a change and is sent
                SetParam(fx, p, kFxParams[fx].defaults[p]);
            }
            engine_->SetFxOn(fx, false);
        }
        ClearChunks();
        chunk_shift_ = false;
        selected_ = 0;
        edited_ = false;
    }

    /** An FX key going down or up. Down selects it for the knobs; SHIFT toggles the latch, a
     *  plain press clears it. Either way the effect is on for as long as the key is held */
    void KeyPressed(size_t fx, bool down, bool shift)
    {
        if (down)
        {
            // detents towards a step belong to the FX they were turned on
            if (fx != selected_)
                ClearChunks();
            selected_ = fx;
            const bool latched = shift ? !latched_[fx] : false;
            if (latched != latched_[fx])
                edited_ = true;
            latched_[fx] = latched;
        }
        held_[fx] = down;
        engine_->SetFxOn(fx, IsOn(fx));
    }

    /** SHIFT going down toggles the latch of every FX key already held, so the combo works in
     *  either order */
    void ShiftPressed()
    {
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            if (!held_[fx])
                continue;
            latched_[fx] = !latched_[fx];
            edited_ = true;
            engine_->SetFxOn(fx, true);
        }
    }

    /** Knob 0-3 turned by detents (signed). Plain: 1% per detent, or a step per
     *  kFxDetentsPerStep for a stepped parameter. SHIFT: one point of the coarse grid per
     *  detent. Knobs past the selected FX's num_params do nothing */
    void KnobTurned(size_t knob, float detents, bool shift)
    {
        const FxParams& fxp = kFxParams[selected_];
        if (knob >= fxp.num_params)
            return;

        const float val = params_[selected_][knob];

        // fine and coarse turns count their detents separately
        if (shift != chunk_shift_)
        {
            ClearChunks();
            chunk_shift_ = shift;
        }

        if (shift)
        {
            // coarse: one grid point per detent
            chunk_[knob] += detents;
            while (chunk_[knob] >= 1.f || chunk_[knob] <= -1.f)
            {
                const float dir = chunk_[knob] > 0.f ? 1.f : -1.f;
                SetParam(selected_, knob, CoarseStep(fxp.coarse[knob], params_[selected_][knob], dir));
                chunk_[knob] -= dir;
            }
            return;
        }

        const uint8_t steps = fxp.steps[knob];
        if (steps == 0)
        {
            SetParam(selected_, knob, val + detents * kFxParamStep);
            return;
        }

        // stepped: every kFxDetentsPerStep detents moves one step
        chunk_[knob] += detents;
        if (chunk_[knob] >= kFxDetentsPerStep || chunk_[knob] <= -kFxDetentsPerStep)
        {
            const float step = 1.f / (steps - 1);
            const float dir = chunk_[knob] > 0.f ? 1.f : -1.f;
            // snap to the step grid, so values set elsewhere can't drift off it
            const float idx = roundf(val / step) + dir;
            SetParam(selected_, knob, idx * step);
            chunk_[knob] = 0.f;
        }
    }

    /** Knob 0-3 pressed: with SHIFT, resets that parameter to its default. A plain press is
     *  kept free for a second parameter page */
    void KnobPressed(size_t knob, bool shift)
    {
        if (!shift || knob >= kFxParams[selected_].num_params)
            return;

        SetParam(selected_, knob, kFxParams[selected_].defaults[knob]);
        chunk_[knob] = 0.f;
    }

    /** The parameters and latches into scene, which is then what the controls match */
    void Snapshot(FxScene& scene)
    {
        scene.used = true;
        scene.latched = 0;
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            if (latched_[fx])
                scene.latched |= static_cast<uint16_t>(1u << fx);
            for (size_t p = 0; p < kNumFxParams; p++)
                scene.params[fx][p] = params_[fx][p];
        }
        edited_ = false;
    }

    /** Recalls scene at the fast slew. Only what changes is sent, so an effect the scenes
     *  share runs on untouched; the latches become the scene's and keys held stay on. On the
     *  device, call it with the audio interrupt blocked, so it lands within one block */
    void Recall(const FxScene& scene)
    {
        engine_->FastFxSlew();
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                if (scene.params[fx][p] != params_[fx][p])
                    SetParam(fx, p, scene.params[fx][p]);
            }
            latched_[fx] = (scene.latched >> fx) & 1;
            engine_->SetFxOn(fx, IsOn(fx));
        }
        ClearChunks();
        edited_ = false;
    }

    /** The grid's next point from val in the direction dir, or val if there is none in
     *  0-1. A value within a hair of a point (1% of the spacing) counts as on it, so it
     *  moves a whole step */
    static float CoarseStep(const FxGrid& grid, float val, float dir)
    {
        if (grid.points)
        {
            const float eps = .001f;
            if (dir > 0.f)
            {
                for (size_t i = 0; i < grid.num_points; i++)
                    if (grid.points[i] > val + eps)
                        return grid.points[i];
            }
            else
            {
                for (size_t i = grid.num_points; i-- > 0;)
                    if (grid.points[i] < val - eps)
                        return grid.points[i];
            }
            return val;
        }

        const float eps = grid.spacing * .01f;
        const float pos = (val - grid.origin) / grid.spacing;
        const float k = dir > 0.f ? floorf(pos + .01f) + 1.f : ceilf(pos - .01f) - 1.f;
        const float next = grid.origin + k * grid.spacing;
        return next < -eps || next > 1.f + eps ? val : next;
    }

    inline float Param(size_t fx, size_t param) const { return params_[fx][param]; }
    inline bool IsHeld(size_t fx) const { return held_[fx]; }
    inline bool IsLatched(size_t fx) const { return latched_[fx]; }
    inline bool IsOn(size_t fx) const { return held_[fx] || latched_[fx]; }
    /** The FX the knobs edit: the last one pressed */
    inline size_t Selected() const { return selected_; }
    /** Knobs or latches changed since the last Snapshot or Recall */
    inline bool Edited() const { return edited_; }
    inline void MarkEdited() { edited_ = true; }

private:
    void ClearChunks()
    {
        for (size_t knob = 0; knob < kNumFxParams; knob++)
            chunk_[knob] = 0.f;
    }

    void SetParam(size_t fx, size_t param, float val)
    {
        val = fclamp(val, 0.f, 1.f);
        if (val != params_[fx][param])
            edited_ = true;
        params_[fx][param] = val;
        engine_->SetFxParam(fx, param, val);
    }

    Engine* engine_ = nullptr;
    float params_[kNumFx][kNumFxParams];
    bool held_[kNumFx];
    bool latched_[kNumFx];
    size_t selected_ = 0;
    float chunk_[kNumFxParams]; // detents towards the next step or grid point
    bool chunk_shift_ = false;  // whether they were turned with SHIFT
    bool edited_ = false;
};

} // namespace chompi
