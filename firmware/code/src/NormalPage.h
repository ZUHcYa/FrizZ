/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h): VOLUME, the looper's keys and transport, the
 *  punch-in FX keys and knobs (FxSlots.h) and the FX scenes (FxScenes.h, SceneStore.h), with
 *  their LEDs. MANUAL.md describes every control; what's here is what the manual doesn't say.
 *
 *  SHIFT is the CHOMPI key held, in either position of the mode switch. The switch does nothing
 *  in play mode; its state is still tracked (switch_state) for later use.
 *
 *  Looper keys: LOOP acts on press so recording starts and stops exactly then. PLAY acts on
 *  release, and only if LOOP wasn't pressed during the hold, so the PLAY + LOOP combos (the
 *  quantized record, the erase) never also toggle play.
 *
 *  FX keys: SHIFT toggles the latch whichever goes down first, so SHIFT going down also
 *  toggles every FX key already held (ShiftPressed). Knobs 1-4 edit the FX pressed last.
 *
 *  Scenes: a recall sends only the parameters that change, within one audio block and at the
 *  fast slew (FxChain::FastSlew), so an FX the two scenes share runs on untouched. The card
 *  is written from MainLoop (SceneStore::Process), never here.
 */
#pragma once

#include "FxSlots.h"
#include "hardware.h"
#include "SceneStore.h"
#include "LedColors.h"
#include "passthroughEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
    static const float kVolumeStep = .01f; // per detent: 1%, like the FX knobs
    static const uint32_t kBattHoldMs = 1250;

    static const float kDefaultOutGain = .75f;
    static const float kDefaultInGain = .75f;
    static const float kDefaultMix = 0.f; // fully dry at power-on, nothing recorded yet

    static const uint8_t kNumPages = 3;

    // encoder IDs, by ui.h's encoder_map: 0-3 are knobs 1-4
    static const uint16_t kTransportEncoder = 4;
    static const uint16_t kVolumeEncoder = 5;

    static const uint8_t kVolumeLed = 9;
    static const uint8_t kChompiKeyLed = 0;
    static const uint8_t kTransportLedRev = 5; // lit when playing in reverse
    static const uint8_t kTransportLedFwd = 6; // lit when playing forward
    static const uint8_t kPlayLed = 7;
    static const uint8_t kLoopLed = 8;
    static const float kPausedDim = .3f;
    static const uint32_t kClosingBlinkMs = 150;
    static const uint32_t kRefusedBlinkMs = 100; // 3 blinks = 6 half-periods
    static const uint32_t kEraseHoldMs = 2000;
    static const float kSpeedStepPerTurn = .25f; // 4 transport detents per speed step

    static const uint8_t kFxKnobLeds[kNumFxParams] = {1, 2, 3, 4}; // PTH LEDs of knobs 1-4
    static const float kFxParamStep = .01f;      // per detent, continuous parameters
    static const float kFxDetentsPerStep = 3.f;  // per step, stepped parameters
    // Knob 1-4 press switches, by ui.h's encoder_map
    static const Hardware::SwId kFxKnobSwitches[kNumFxParams] = {
        Hardware::SwId::ENC_4_SW,
        Hardware::SwId::ENC_1_SW,
        Hardware::SwId::ENC_2_SW,
        Hardware::SwId::ENC_3_SW,
    };
    // FX key LEDs. The SMT LEDs have 64 steps (temp_led_stuff.h), and below about 8 of them
    // the colours run together, so off is as dim as the keys go while keeping their colour.
    static const float kFxOffLevel = .15f;    // off: every FX key dimly in its colour
    static const float kFxMeterFloorDb = -30.f; // the meters' range, up to 0 dBFS
    static const float kFxWhiteMax = .8f;     // on: how far the loudest audio pushes to white

    // FX scenes: the slots on KEY_16-19, and SAVE / COPY / DELETE on TAPE's preset keys in
    // TEMPO's colours
    static const Hardware::SwId kSceneKeys[kNumScenes] = {
        Hardware::SwId::KEY_16,
        Hardware::SwId::KEY_17,
        Hardware::SwId::KEY_18,
        Hardware::SwId::KEY_19,
    };
    static const uint8_t kSceneLeds[kNumScenes] = {0, 1, 2, 3};
    enum class SceneMode
    {
        NONE,
        SAVE,
        COPY,
        DELETE,
    };
    struct SceneModeKey
    {
        SceneMode mode;
        Hardware::SwId key;
        uint8_t led;
        const float* color;
    };
    static const SceneModeKey kSceneModeKeys[] = {
        {SceneMode::SAVE, Hardware::SwId::KEY_25, 9, blue},
        {SceneMode::COPY, Hardware::SwId::KEY_24, 8, green},
        {SceneMode::DELETE, Hardware::SwId::KEY_23, 7, red},
    };
    static const int kNoScene = -1;
    static const uint32_t kSceneBlinkMs = 250;      // a selected slot, the armed CHOMPI key
    static const uint32_t kSceneFlashMs = 100;      // the confirmation: 3 fast blinks
    static const uint32_t kSceneEmptyBlinkMs = 300; // an empty slot pressed
    static const uint32_t kScenePulseMs = 1000;     // the active scene, edited

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(PassthroughEngine *engine, Hardware *hw, SceneStore *scenes)
        {
            hw_ = hw;
            engine_ = engine;
            scenes_ = scenes;

            out_gain_ = kDefaultOutGain;
            in_gain_ = kDefaultInGain;
            final_comp_ = 0.f;
            mix_ = kDefaultMix;
            page_ = 0;

            // the engine only hears about a value when it changes, so push them all now
            engine_->SetMainGain(out_gain_);
            engine_->SetInputGain(in_gain_);
            engine_->SetFinalComp(final_comp_);
            engine_->SetMix(mix_);

            for (size_t fx = 0; fx < kNumFx; fx++)
            {
                for (size_t p = 0; p < kNumFxParams; p++)
                    SetFxParam(fx, p, kFxSlots[fx].defaults[p]);
                engine_->SetFxOn(fx, false);
            }

            ResetSmtLeds();
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            init_time = System::GetNow();
        }

        void ResetSmtLeds()
        {
            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore && now - init_time > 1500)
                init_ignore = false;

            // the boot / rainbow animations leave the other knob LEDs lit, and nothing
            // clears the canvas, so blank them all every frame
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            float r, g, b;

            if (batt_display && now - batt_hold > kBattHoldMs)
            {
                const float* color = &green[0];

                switch(hw_->GetBatteryLevel())
                {
                    case Hardware::BatteryLevel::FULL:   color = &white[0];  break;
                    case Hardware::BatteryLevel::HIGH:   color = &green[0];  break;
                    case Hardware::BatteryLevel::MEDIUM: color = &yellow[0]; break;
                    case Hardware::BatteryLevel::LOW:    color = &red[0];    break;
                    default: break;
                }

                r = color[0];
                g = color[1];
                b = color[2];
            }
            else if (Shift())
            {
                r = color_xfade(green[0], purple[0], mix_);
                g = color_xfade(green[1], purple[1], mix_);
                b = color_xfade(green[2], purple[2], mix_);
            }
            else if (page_ == 0)
            {
                float vu_sample = engine_->GetVUSample();

                r = out_gain_ * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                g = out_gain_ * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                b = out_gain_ * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
            }
            else if (page_ == 1)
            {
                r = color_xfade(blue[0], red[0], in_gain_);
                g = color_xfade(blue[1], red[1], in_gain_);
                b = color_xfade(blue[2], red[2], in_gain_);
            }
            else
            {
                r = med_blue[0] * (final_comp_ * .9f + .1f);
                g = med_blue[1] * (final_comp_ * .9f + .1f);
                b = med_blue[2] * (final_comp_ * .9f + .1f);
            }
            SetPthLedFloat(kVolumeLed, r, g, b);

            // hold PLAY + LOOP to erase
            if (erase_armed_ && now - erase_hold_ >= kEraseHoldMs)
            {
                engine_->looper.Erase();
                SetMix(0.f); // the input fades in while the loop fades out
                erase_armed_ = false;
            }

            // jump to fully wet when a recording closes into playback
            const Looper::State looper_state = engine_->looper.GetState();
            if (last_looper_state_ == Looper::State::RECORDING && looper_state == Looper::State::PLAYING)
                SetMix(1.f);
            last_looper_state_ = looper_state;

            DrawLooperLeds(now);
            DrawFxLeds();
            DrawSceneLeds(now);

            // CHOMPI key: blinking red while it would confirm a scene action, otherwise
            // white while it is acting as SHIFT
            if (SceneArmed())
            {
                r = (now / kSceneBlinkMs) % 2 == 0 ? 1.f : 0.f;
                g = b = 0.f;
            }
            else
                r = g = b = Shift() ? 1.f : 0.f;
            SetPthLedFloat(kChompiKeyLed, r, g, b);

            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
                return false;

            bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            // short press cycles the pages, hold to check battery level
            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && !Shift() && System::GetNow() - batt_hold < kBattHoldMs)
                    page_ = (page_ + 1) % kNumPages;

                batt_hold = System::GetNow();
                batt_display = rising;
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
                chompi_key_pressed = rising;
                if (rising && SceneArmed())
                    ConfirmScene();
                else if (rising)
                    ShiftPressed();
                break;

            // transport press: back to 1x forward
            case ENC_5_SW:
                if (rising && LoopExists())
                {
                    engine_->looper.ResetSpeed();
                    speed_chunk_ = 0.f;
                }
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // PLAY
                play_pressed_ = rising;
                if (rising)
                {
                    play_combo_ = false;
                    // LOOP held first, then PLAY: same erase combo
                    if (loop_pressed_ && LoopExists())
                    {
                        play_combo_ = true;
                        ArmErase();
                    }
                }
                else
                {
                    erase_armed_ = false;
                    if (!play_combo_)
                        engine_->looper.TogglePlay();
                }
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // LOOP
                loop_pressed_ = rising;
                if (rising)
                    LoopPressed();
                else
                    erase_armed_ = false;
                break;

            default:
                for (size_t s = 0; s < kNumScenes; s++)
                {
                    if (rising && buttonID == static_cast<uint16_t>(kSceneKeys[s]))
                        ScenePressed(s);
                }
                for (const SceneModeKey& key : kSceneModeKeys)
                {
                    if (rising && buttonID == static_cast<uint16_t>(key.key))
                        SceneModePressed(key.mode);
                }
                for (size_t fx = 0; fx < kNumFx; fx++)
                {
                    if (buttonID == static_cast<uint16_t>(kFxSlots[fx].key))
                        FxKeyPressed(fx, rising);
                }
                for (size_t knob = 0; knob < kNumFxParams; knob++)
                {
                    if (rising && buttonID == static_cast<uint16_t>(kFxKnobSwitches[knob]))
                        FxKnobPressed(knob);
                }
                break;
            }

            return true;
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            const float detents = Detents(encoderID, turns);
            if (encoderID == kTransportEncoder)
                TransportTurned(turns);
            else if (encoderID < kNumFxParams)
                FxKnobTurned(encoderID, detents);
            else if (encoderID == kVolumeEncoder)
                VolumeTurned(detents);
            else
                return false;
            return true;
        }

        void SetSwitchState(bool state) { switch_state = state; }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        /** Detents turned. ui.h sends knob 1 and the transport 1x per detent and the other
         *  knobs 3x (TestPage relies on that), so this undoes it */
        static float Detents(uint16_t encoder, int16_t turns)
        {
            return encoder == 0 || encoder == kTransportEncoder ? turns : turns / 3.f;
        }

        void VolumeTurned(float detents)
        {
            const float inc = detents * kVolumeStep;

            if (Shift())
                SetMix(mix_ + inc);
            else if (page_ == 0)
            {
                out_gain_ = fclamp(out_gain_ + inc, 0.f, 1.f);
                engine_->SetMainGain(out_gain_);
            }
            else if (page_ == 1)
            {
                in_gain_ = fclamp(in_gain_ + inc, 0.f, 1.f);
                engine_->SetInputGain(in_gain_);
            }
            else
            {
                final_comp_ = fclamp(final_comp_ + inc, 0.f, 1.f);
                engine_->SetFinalComp(final_comp_);
            }
        }

        void LoopPressed()
        {
            if (play_pressed_)
                play_combo_ = true; // PLAY's release must not toggle

            Looper& looper = engine_->looper;
            switch (looper.GetState())
            {
            case Looper::State::EMPTY:
                if (!play_pressed_)
                    looper.StartRecording(false);
                else if (looper.CanRecordQuantized())
                    looper.StartRecording(true);
                else
                    record_refused_ = System::GetNow();
                break;

            case Looper::State::RECORDING:
                looper.StopRecording();
                break;

            case Looper::State::PLAYING:
            case Looper::State::PAUSED:
                // only a loop that already existed when both went down can be erased, so
                // holding the quantized-record combo can't erase the new recording.
                // Either key may go down first.
                if (play_pressed_)
                    ArmErase();
                break;
            }
        }

        void SetMix(float mix)
        {
            mix_ = fclamp(mix, 0.f, 1.f);
            engine_->SetMix(mix_);
        }

        void FxKeyPressed(size_t fx, bool rising)
        {
            if (rising)
            {
                fx_selected_ = fx;
                // SHIFT toggles the latch, a plain press clears it; either way the effect
                // is on for as long as the key is held
                const bool latched = Shift() ? !fx_latched_[fx] : false;
                if (latched != fx_latched_[fx])
                    scene_edited_ = true;
                fx_latched_[fx] = latched;
            }
            fx_held_[fx] = rising;
            engine_->SetFxOn(fx, fx_held_[fx] || fx_latched_[fx]);
        }

        // SHIFT going down toggles the latch of every FX key already held, so the combo
        // works in either order
        void ShiftPressed()
        {
            for (size_t fx = 0; fx < kNumFx; fx++)
            {
                if (!fx_held_[fx])
                    continue;
                fx_latched_[fx] = !fx_latched_[fx];
                scene_edited_ = true;
                engine_->SetFxOn(fx, true);
            }
        }

        void FxKnobTurned(uint16_t knob, float detents)
        {
            if (knob >= kFxSlots[fx_selected_].num_params)
                return;

            float& val = fx_params_[fx_selected_][knob];

            if (Shift())
            {
                // coarse: one grid point per detent
                fx_step_chunk_[knob] += detents;
                while (fx_step_chunk_[knob] >= 1.f || fx_step_chunk_[knob] <= -1.f)
                {
                    const float dir = fx_step_chunk_[knob] > 0.f ? 1.f : -1.f;
                    SetFxParam(fx_selected_, knob, CoarseStep(kFxSlots[fx_selected_].coarse[knob], val, dir));
                    fx_step_chunk_[knob] -= dir;
                }
                return;
            }

            const uint8_t steps = kFxSlots[fx_selected_].steps[knob];
            if (steps == 0)
            {
                SetFxParam(fx_selected_, knob, val + detents * kFxParamStep);
                return;
            }

            // stepped: every kFxDetentsPerStep detents moves one step
            fx_step_chunk_[knob] += detents;
            if (fx_step_chunk_[knob] >= kFxDetentsPerStep || fx_step_chunk_[knob] <= -kFxDetentsPerStep)
            {
                const float step = 1.f / (steps - 1);
                const float dir = fx_step_chunk_[knob] > 0.f ? 1.f : -1.f;
                // snap to the step grid, so values set elsewhere can't drift off it
                const float idx = roundf(val / step) + dir;
                SetFxParam(fx_selected_, knob, idx * step);
                fx_step_chunk_[knob] = 0.f;
            }
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

        void FxKnobPressed(size_t knob)
        {
            // a plain press is kept free for a second parameter page
            if (!Shift() || knob >= kFxSlots[fx_selected_].num_params)
                return;

            SetFxParam(fx_selected_, knob, kFxSlots[fx_selected_].defaults[knob]);
            fx_step_chunk_[knob] = 0.f;
        }

        void SetFxParam(size_t fx, size_t param, float val)
        {
            val = fclamp(val, 0.f, 1.f);
            if (val != fx_params_[fx][param])
                scene_edited_ = true;
            fx_params_[fx][param] = val;
            engine_->SetFxParam(fx, param, val);
        }

        void ScenePressed(size_t slot)
        {
            const bool used = scenes_->scenes[slot].used;
            const int s = static_cast<int>(slot);
            switch (scene_mode_)
            {
            case SceneMode::NONE:
                if (used)
                    RecallScene(slot);
                else
                    SceneEmptyBlink(slot);
                break;
            case SceneMode::SAVE:
                scene_sel_ = s;
                break;
            case SceneMode::COPY:
                if (scene_src_ == kNoScene)
                {
                    if (used)
                        scene_src_ = s;
                    else
                        SceneEmptyBlink(slot);
                }
                else if (s != scene_src_)
                    scene_sel_ = s;
                break;
            case SceneMode::DELETE:
                if (used)
                    scene_sel_ = s;
                else
                    SceneEmptyBlink(slot);
                break;
            }
        }

        // the same mode key again cancels, another switches over
        void SceneModePressed(SceneMode mode)
        {
            scene_mode_ = scene_mode_ == mode ? SceneMode::NONE : mode;
            scene_sel_ = scene_src_ = kNoScene;
        }

        inline bool SceneArmed() const
        {
            return scene_mode_ != SceneMode::NONE && scene_sel_ != kNoScene;
        }

        void ConfirmScene()
        {
            FxScene* scenes = scenes_->scenes;
            FxScene& target = scenes[scene_sel_];
            switch (scene_mode_)
            {
            case SceneMode::SAVE:
                target.used = true;
                target.latched = 0;
                for (size_t fx = 0; fx < kNumFx; fx++)
                {
                    if (fx_latched_[fx])
                        target.latched |= static_cast<uint16_t>(1u << fx);
                    for (size_t p = 0; p < kNumFxParams; p++)
                        target.params[fx][p] = fx_params_[fx][p];
                }
                active_scene_ = scene_sel_;
                scene_edited_ = false;
                break;
            case SceneMode::COPY:
                target = scenes[scene_src_];
                // the sound stays, so it no longer matches the active scene
                if (active_scene_ == scene_sel_)
                    scene_edited_ = true;
                break;
            case SceneMode::DELETE:
                target.used = false;
                if (active_scene_ == scene_sel_)
                    active_scene_ = kNoScene;
                break;
            case SceneMode::NONE:
                return;
            }

            scenes_->RequestSave();
            scene_flash_ = scene_sel_;
            scene_flash_time_ = System::GetNow();
            scene_flash_ok_ = scenes_->CardOk();
            scene_mode_ = SceneMode::NONE;
            scene_sel_ = scene_src_ = kNoScene;
        }

        void RecallScene(size_t slot)
        {
            const FxScene& scene = scenes_->scenes[slot];
            {
                // the whole scene within one audio block
                ScopedIrqBlocker irq;
                engine_->FastFxSlew();
                for (size_t fx = 0; fx < kNumFx; fx++)
                {
                    // only what changes, so an effect the scenes share runs on untouched
                    for (size_t p = 0; p < kNumFxParams; p++)
                    {
                        if (scene.params[fx][p] != fx_params_[fx][p])
                            SetFxParam(fx, p, scene.params[fx][p]);
                    }
                    fx_latched_[fx] = (scene.latched >> fx) & 1;
                    engine_->SetFxOn(fx, fx_held_[fx] || fx_latched_[fx]);
                }
            }
            for (size_t knob = 0; knob < kNumFxParams; knob++)
                fx_step_chunk_[knob] = 0.f;
            active_scene_ = static_cast<int>(slot);
            scene_edited_ = false;
        }

        void SceneEmptyBlink(size_t slot)
        {
            scene_empty_ = static_cast<int>(slot);
            scene_empty_time_ = System::GetNow();
        }

        void DrawSceneLeds(uint32_t now)
        {
            const float* mode_color = white;
            for (const SceneModeKey& key : kSceneModeKeys)
            {
                const bool on = scene_mode_ == key.mode;
                if (on)
                    mode_color = key.color;
                const float level = on ? 1.f : kFxOffLevel;
                SetSmtLedFloat(key.led, level * key.color[0], level * key.color[1], level * key.color[2]);
            }

            const bool blink_on = (now / kSceneBlinkMs) % 2 == 0;
            for (size_t slot = 0; slot < kNumScenes; slot++)
            {
                const int s = static_cast<int>(slot);
                const float* color = white;
                float level = 0.f;
                if (s == scene_flash_ && now - scene_flash_time_ < 6 * kSceneFlashMs)
                {
                    color = scene_flash_ok_ ? green : red;
                    level = ((now - scene_flash_time_) / kSceneFlashMs) % 2 == 0 ? 1.f : 0.f;
                }
                else if (s == scene_empty_ && now - scene_empty_time_ < kSceneEmptyBlinkMs)
                {
                    color = red;
                    level = 1.f;
                }
                else if (scene_mode_ != SceneMode::NONE && s == scene_src_)
                {
                    color = mode_color;
                    level = 1.f;
                }
                else if (scene_mode_ != SceneMode::NONE && s == scene_sel_)
                {
                    color = mode_color;
                    level = blink_on ? 1.f : 0.f;
                }
                else if (s == active_scene_)
                {
                    // edited: a slow pulse between the saved and the active brightness
                    const float phase = static_cast<float>(now % kScenePulseMs) / kScenePulseMs;
                    level = scene_edited_ ? .6f + .4f * cosf(phase * TWOPI_F) : 1.f;
                }
                else if (scenes_->scenes[slot].used)
                    level = kFxOffLevel;
                SetSmtLedFloat(kSceneLeds[slot], level * color[0], level * color[1], level * color[2]);
            }
        }

        void DrawFxLeds()
        {
            // knob LEDs: the selected FX's parameters in its colours
            const float* const* colors = kFxSlots[fx_selected_].knob_colors;
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                const float val = fx_params_[fx_selected_][p];
                if (p >= kFxSlots[fx_selected_].num_params)
                    SetPthLedFloat(kFxKnobLeds[p], 0.f, 0.f, 0.f);
                else
                    SetPthLedFloat(kFxKnobLeds[p],
                                   color_triple_xfade(colors[0][0], colors[1][0], colors[2][0], val),
                                   color_triple_xfade(colors[0][1], colors[1][1], colors[2][1], val),
                                   color_triple_xfade(colors[0][2], colors[1][2], colors[2][2], val));
            }

            for (size_t fx = 0; fx < kNumFx; fx++)
            {
                const float* color = kFxSlots[fx].key_color;
                // the meter (about the output's amplitude) on a dB scale: 0 at the floor, 1 at 0 dBFS
                const float db = 20.f * log10f(fmaxf(engine_->GetFxLevel(fx), 1e-6f));
                const float meter = fclamp(1.f - db / kFxMeterFloorDb, 0.f, 1.f);
                float level = kFxOffLevel;
                float white = 0.f;
                if (fx_held_[fx] || fx_latched_[fx])
                {
                    level = 1.f;
                    // squared, so normal levels stay coloured and the peaks flash white
                    white = kFxWhiteMax * meter * meter;
                }
                else if (kFxSlots[fx].kind == FxKind::SEND)
                {
                    // a send's tail ringing out, from full down to off, in even steps to the eye
                    level = kFxOffLevel * powf(1.f / kFxOffLevel, meter);
                }
                SetSmtLedFloat(kFxSlots[fx].key_led,
                               level * (color[0] + white * (1.f - color[0])),
                               level * (color[1] + white * (1.f - color[1])),
                               level * (color[2] + white * (1.f - color[2])));
            }
        }

        void DrawLooperLeds(uint32_t now)
        {
            const Looper& looper = engine_->looper;
            const Looper::State state = looper.GetState();

            float play = 0.f;  // PLAY LED, white level
            float loop[3] = {0.f, 0.f, 0.f};

            if (state == Looper::State::RECORDING)
            {
                const bool on = !looper.IsClosing() || (now / kClosingBlinkMs) % 2 == 0;
                loop[0] = on ? 1.f : 0.f;
            }
            else if (state == Looper::State::PLAYING || state == Looper::State::PAUSED)
            {
                const float level = state == Looper::State::PLAYING ? 1.f : kPausedDim;
                const float pos = looper.GetPosition();
                play = (1.f - pos) * level;
                loop[0] = loop[1] = loop[2] = pos * level;
            }

            // refused quantized record: 3 fast red blinks, over whatever LOOP was showing
            if (record_refused_ && now - record_refused_ < 6 * kRefusedBlinkMs)
            {
                const bool on = ((now - record_refused_) / kRefusedBlinkMs) % 2 == 0;
                loop[0] = on ? 1.f : 0.f;
                loop[1] = loop[2] = 0.f;
            }

            SetPthLedFloat(kPlayLed, play, play, play);
            SetPthLedFloat(kLoopLed, loop[0], loop[1], loop[2]);

            if (state == Looper::State::PLAYING)
                DrawSpeedLeds(looper.GetSpeed());
            else if (state == Looper::State::PAUSED)
            {
                // scrub speed in white on the LED for its direction
                const float scrub = looper.GetScrub() * .5f;
                const float level = fabsf(scrub);
                SetPthLedFloat(scrub > 0.f ? kTransportLedFwd : kTransportLedRev, level, level, level);
            }
        }

        /** TAPE's transport colours: speed -2..2 maps to 0..1; blue at the extremes through
         *  green and yellow to red towards a stop. The LED for the direction is lit, and the
         *  other one glows red as the speed nears zero. */
        void DrawSpeedLeds(float speed)
        {
            const float value = speed * .25f + .5f;
            const float idx = value < .5f ? value * 2.f : (1.f - value) * 2.f; // 0 - 1 - 0
            const uint8_t led_on = value > .5f ? kTransportLedFwd : kTransportLedRev;
            const uint8_t led_off = value > .5f ? kTransportLedRev : kTransportLedFwd;

            SetPthLedFloat(led_on,
                           color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx),
                           color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx),
                           color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx));

            if (idx > .8f)
            {
                const float dim = (idx - .8f) * 5.f;
                SetPthLedFloat(led_off, red[0] * dim, red[1] * dim, red[2] * dim);
            }
        }

        void TransportTurned(int16_t turns)
        {
            if (Shift())
                return;

            Looper& looper = engine_->looper;
            if (looper.GetState() == Looper::State::PAUSED)
                looper.Scrub(turns);
            else if (looper.GetState() == Looper::State::PLAYING)
            {
                speed_chunk_ += turns * kSpeedStepPerTurn;
                if (speed_chunk_ >= 1.f || speed_chunk_ <= -1.f)
                {
                    looper.StepSpeed(speed_chunk_ > 0.f ? 1 : -1);
                    speed_chunk_ = 0.f;
                }
            }
        }

        inline bool LoopExists()
        {
            const Looper::State state = engine_->looper.GetState();
            return state == Looper::State::PLAYING || state == Looper::State::PAUSED;
        }

        inline void ArmErase()
        {
            erase_armed_ = true;
            erase_hold_ = System::GetNow();
        }

        inline bool Shift() { return chompi_key_pressed; }

        Hardware *hw_;
        PassthroughEngine *engine_;
        SceneStore *scenes_;

        Looper::State last_looper_state_ = Looper::State::EMPTY;

        float out_gain_;
        float in_gain_;
        float final_comp_;
        float mix_;
        uint8_t page_;

        bool switch_state = false; // true with the mode switch DOWN; unused in play mode for now
        bool chompi_key_pressed = false;

        bool play_pressed_ = false;
        bool loop_pressed_ = false;
        bool play_combo_ = false;   // LOOP was pressed during this PLAY hold
        bool erase_armed_ = false;  // PLAY + LOOP held on an existing loop
        uint32_t erase_hold_ = 0;
        uint32_t record_refused_ = 0;
        float speed_chunk_ = 0.f;   // transport detents towards the next speed step

        float fx_params_[kNumFx][kNumFxParams];
        bool fx_held_[kNumFx] = {};
        bool fx_latched_[kNumFx] = {};
        size_t fx_selected_ = 0;    // the FX the knobs edit: the last one pressed
        float fx_step_chunk_[kNumFxParams] = {}; // detents towards the next step or grid point

        SceneMode scene_mode_ = SceneMode::NONE;
        int scene_sel_ = kNoScene;     // the slot SAVE / COPY / DELETE acts on
        int scene_src_ = kNoScene;     // COPY's source
        int active_scene_ = kNoScene;  // the last one recalled or saved
        bool scene_edited_ = false;    // knobs or latches changed since
        int scene_flash_ = kNoScene;   // confirmed, flashing
        uint32_t scene_flash_time_ = 0;
        bool scene_flash_ok_ = false;  // saved to the card
        int scene_empty_ = kNoScene;   // empty, pressed
        uint32_t scene_empty_time_ = 0;

        bool batt_display = false; // VOLUME held
        uint32_t batt_hold = 0;     // when VOLUME was last pressed or released
    };

} // namespace chompi
