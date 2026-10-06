/** @file FxControls.h
 *  @brief What the play page does with the punch-in FX keys and knobs 1-4, without the
 *  hardware: the parameters, which keys are held and latched, which FX the knobs edit, fine,
 *  stepped and coarse turns, and taking, recalling or morphing to a scene. NormalPage.h routes the keys and
 *  knobs here and draws the LEDs from it; test/controls.cpp runs it on the host.
 *
 *  Engine is PassthroughEngine on the device, a fake on the host. It needs SetFxOn(fx, on),
 *  SetFxParam(fx, param, val), FastFxSlew() and the morph's StartFxMorph(plan),
 *  AddFxMorphBar(), LandFxMorph(), FreezeFxMorph(params, unswitched, was_on) and
 *  FxMorphing().
 */
#pragma once
#include <math.h>
#include "FxMorph.h"
#include "FxParams.h"
#include "FxScenes.h"

namespace chompi
{

// Knobs: 1% per detent; a stepped parameter moves one step per kFxDetentsPerStep detents
static const float kFxParamStep = .01f;
static const float kFxDetentsPerStep = 3.f;
// The sends: their tails ring on after their keys go off, so a recall or morph doesn't change
// an off send's settings in the engine until its key comes on again
static const uint16_t kFxSends = (1u << FX_DELAY) | (1u << FX_REVERB);

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
        stale_ = morph_touched_ = 0;
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
            TouchedInMorph(fx);
        }
        held_[fx] = down;
        SendOn(fx);
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
            TouchedInMorph(fx);
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
     *  share runs on untouched; the latches become the scene's and keys held stay on. A send
     *  the scene leaves off keeps ringing out as it was (kFxSends). On the device, call it with
     *  the audio interrupt blocked, so it lands within one block */
    void Recall(const FxScene& scene)
    {
        engine_->LandFxMorph();
        engine_->FastFxSlew();
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            latched_[fx] = (scene.latched >> fx) & 1;
            const bool spare = !IsOn(fx) && (kFxSends >> fx & 1);
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                if (scene.params[fx][p] == params_[fx][p])
                    continue;
                if (spare)
                {
                    params_[fx][p] = scene.params[fx][p];
                    stale_ |= Bit(fx);
                }
                else
                    SetParam(fx, p, scene.params[fx][p]);
            }
            SendOn(fx);
        }
        ClearChunks();
        edited_ = false;
    }

    /** Glides to scene, landing on the FX clock's next bar line (FxMorph.h). The controls
     *  show the scene at once: knobs turned and keys pressed meanwhile change where it lands.
     *  Per FX, from what it is now to what the scene makes it (held keys stay on):
     *   - off in both: the parameters jump now, unheard, except a send's, which rings out as
     *     it was (kFxSends);
     *   - on in both: continuous parameters glide, stepped ones switch at the landing;
     *   - one it turns on: its fade knobs (FxParams::fade) start at their neutral defaults
     *     and glide to the scene, the rest jump now; the key comes on as soon as they've got
     *     there;
     *   - one it turns off: its fade knobs glide to their defaults and the key goes off at the
     *     landing; the engine keeps it there, silent, until its key next comes on;
     *   - an FX without fade knobs it turns on or off: all at the landing.
     *  Only what changes is sent, as in a recall
     *  On the device, call it with the audio interrupt blocked */
    void Morph(const FxScene& scene)
    {
        engine_->LandFxMorph();
        FxMorphPlan plan;
        plan.deferred = plan.wake = plan.was_on = plan.park = 0;
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            const FxParams& fxp = kFxParams[fx];
            const bool was = IsOn(fx);
            const bool will = held_[fx] || ((scene.latched >> fx) & 1);
            const uint16_t bit = Bit(fx);
            if (was)
                plan.was_on |= bit;
            if (was != will)
            {
                plan.deferred |= bit;
                if (will && fxp.fade)
                    plan.wake |= bit;
            }
            if (was && !will)
            {
                // parked at the landing: the engine lags the controls until it's on again
                plan.park |= bit;
                stale_ |= bit;
            }
            if (!was && !will && (kFxSends & bit))
            {
                // a send ringing out: untouched, it takes the scene when it next comes on
                for (size_t p = 0; p < kNumFxParams; p++)
                {
                    plan.start[fx][p] = plan.target[fx][p] = scene.params[fx][p];
                    plan.how[fx][p] = MorphParam::HOLD;
                    if (scene.params[fx][p] != params_[fx][p])
                        stale_ |= bit;
                }
                continue;
            }
            const bool stale = Stale(fx) && !was;
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                const float to = scene.params[fx][p];
                const bool fade = (fxp.fade >> p) & 1;
                float from = params_[fx][p];
                MorphParam how = MorphParam::HOLD;
                if (!was && !will)
                    from = to;
                else if (was && will)
                    how = fxp.steps[p] == 0 ? MorphParam::GLIDE : MorphParam::HOLD;
                else if (fxp.fade && will)
                {
                    from = fade ? fxp.defaults[p] : to;
                    how = fade ? MorphParam::GLIDE : MorphParam::HOLD;
                }
                else if (fxp.fade && fade)
                    how = MorphParam::FADE_OUT;
                plan.start[fx][p] = from;
                plan.target[fx][p] = to;
                plan.how[fx][p] = how;
                // the morph starts from what the engine has
                if (stale || from != params_[fx][p])
                    engine_->SetFxParam(fx, p, from);
            }
            if (stale)
                stale_ &= static_cast<uint16_t>(~bit);
        }
        engine_->StartFxMorph(plan);

        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            latched_[fx] = (scene.latched >> fx) & 1;
            for (size_t p = 0; p < kNumFxParams; p++)
                params_[fx][p] = scene.params[fx][p];
            engine_->SetFxOn(fx, IsOn(fx));
        }
        morph_touched_ = 0;
        ClearChunks();
        edited_ = false;
    }

    /** One bar line more for the morph, up to kMaxMorphBars. False if there's none running or
     *  it's at the most. On the device, call it with the audio interrupt blocked */
    inline bool MorphMore() { return engine_->AddFxMorphBar(); }
    inline bool Morphing() const { return engine_->FxMorphing(); }

    /** Stops the morph where it is: the knobs take the values it got to, and an FX it hadn't
     *  switched yet stays as it was, unless its key was pressed meanwhile. Edited, since
     *  that's neither scene. False if none runs. On the device, call it with the audio
     *  interrupt blocked */
    bool FreezeMorph()
    {
        float live[kNumFx][kNumFxParams];
        uint16_t unswitched, was_on;
        if (!engine_->FreezeFxMorph(live, &unswitched, &was_on))
            return false;
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            const uint16_t bit = Bit(fx);
            // a send left ringing out keeps the scene for when it comes on
            if (Stale(fx) && !(unswitched & bit))
                continue;
            for (size_t p = 0; p < kNumFxParams; p++)
                params_[fx][p] = live[fx][p];
            stale_ &= static_cast<uint16_t>(~bit);
            if (unswitched & bit)
            {
                if (!(morph_touched_ & bit))
                    latched_[fx] = was_on & bit;
                SendOn(fx); // a key held meanwhile stays on
            }
        }
        ClearChunks();
        edited_ = true;
        return true;
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
    static inline uint16_t Bit(size_t fx) { return static_cast<uint16_t>(1u << fx); }
    inline bool Stale(size_t fx) const { return (stale_ >> fx) & 1; }

    /** The key's state to the engine; coming on, first the parameters it lags behind in */
    void SendOn(size_t fx)
    {
        if (IsOn(fx) && Stale(fx))
        {
            for (size_t p = 0; p < kNumFxParams; p++)
                engine_->SetFxParam(fx, p, params_[fx][p]);
            stale_ &= static_cast<uint16_t>(~Bit(fx));
        }
        engine_->SetFxOn(fx, IsOn(fx));
    }

    /** A key pressed during a morph: it decides that FX's latch, also if the morph stops */
    void TouchedInMorph(size_t fx)
    {
        if (engine_->FxMorphing())
            morph_touched_ |= Bit(fx);
    }

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
    uint16_t stale_ = 0;         // bit fx: off, and the engine has older parameters than
                                 // params_: a send ringing out, or an FX a morph parked
    uint16_t morph_touched_ = 0; // bit fx: its key pressed during the morph
};

} // namespace chompi
