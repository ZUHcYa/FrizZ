/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h): VOLUME, the looper's keys and transport, the
 *  punch-in FX keys and knobs (FxControls.h, FxSlots.h) and the FX scenes (SceneControls.h,
 *  SceneStore.h), with their LEDs. The logic is in FxControls.h and SceneControls.h; this
 *  routes the hardware to it and draws. MANUAL.md describes every control; what's here is what the manual doesn't say.
 *
 *  SHIFT is the CHOMPI key held, in either position of the mode switch. The switch picks the
 *  headphone feed: down mirrors the master out, up is the dry input (passthroughEngine.h).
 *
 *  The CHOMPI, PLAY and LOOP keys' rules are in PlayKeys.h: SHIFT, the confirm tap in a scene
 *  mode, the looper's combos, tap tempo (TapTempo.h). This page is its Host. Every other key
 *  or knob used tells it (Used, FxKey), so SHIFT + it is a combo rather than a confirm.
 *
 *  FX keys: an FX key held, then SHIFT, toggles the latch, decided on the key's release;
 *  SHIFT, then an FX key, only selects it for the knobs, silently (FxControls.h), and the key
 *  flashes white. Knobs 1-4 edit the FX pressed or selected last.
 *
 *  Scenes: a recall sends only the parameters that change, within one audio block and at the
 *  fast slew (FxChain::FastSlew), so an FX the two scenes share runs on untouched. SHIFT +
 *  a scene key morphs to it instead (FxMorph.h), landing on a bar line; the engine times it.
 *  SHIFT + PLAY stops a morph where it is; without one, PLAY works as ever.
 *  The card is written from MainLoop (SceneStore::Process), never here.
 */
#pragma once

#include "FxControls.h"
#include "FxSlots.h"
#include "hardware.h"
#include "LedSignal.h"
#include "PlayKeys.h"
#include "SceneControls.h"
#include "TapTempo.h"
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
    static const uint32_t kTapFlashMs = 80;      // LOOP flashes white on a tempo tap
    static const uint32_t kSelectFlashMs = 80;   // an FX key flashes white on a select
    static const float kSpeedStepPerTurn = .25f; // 4 transport detents per speed step

    static const uint8_t kFxKnobLeds[kNumFxParams] = {1, 2, 3, 4}; // PTH LEDs of knobs 1-4
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

    // FX scenes: the slots on KEY_16-20, the lower octave's dark keys, the blank one first;
    // SAVE / COPY / DELETE on TAPE's preset keys in TEMPO's colours. One language for all
    // three: the mode's colour shows what will happen (the slots it can act on, the pick, the
    // CHOMPI key that confirms), white that it's done, red that it was refused or isn't on
    // the card
    static const Hardware::SwId kSceneKeys[kNumSlots] = {
        Hardware::SwId::KEY_16,
        Hardware::SwId::KEY_17,
        Hardware::SwId::KEY_18,
        Hardware::SwId::KEY_19,
        Hardware::SwId::KEY_20,
    };
    static const uint8_t kSceneLeds[kNumSlots] = {0, 1, 2, 3, 4};
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
    static const uint32_t kSceneBlinkMs = 250;      // a picked slot, the armed CHOMPI key
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

            fx_.Init(engine_);
            scene_ctl_.Init(scenes_->scenes, &fx_);
            keys_.Init(this);

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
            const uint32_t now = System::GetNow();
            Update(now);

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
                Xfade(green, purple, mix_, &r, &g, &b);
            else if (page_ == 0)
            {
                float vu_sample = engine_->GetVUSample();

                r = out_gain_ * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                g = out_gain_ * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                b = out_gain_ * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
            }
            else if (page_ == 1)
                Xfade(blue, red, in_gain_, &r, &g, &b);
            else
            {
                r = med_blue[0] * (final_comp_ * .9f + .1f);
                g = med_blue[1] * (final_comp_ * .9f + .1f);
                b = med_blue[2] * (final_comp_ * .9f + .1f);
            }
            SetPthLedFloat(kVolumeLed, r, g, b);

            DrawLooperLeds(now);
            DrawFxLeds(now);
            DrawSceneLeds(now);

            // CHOMPI key: blinking in the mode's colour while a tap would confirm a scene
            // action, otherwise white while it is acting as SHIFT
            if (scene_ctl_.Armed() && !keys_.ShiftCombo())
                PthLed(kChompiKeyLed, SceneModeColor(), (now / kSceneBlinkMs) % 2 == 0 ? 1.f : 0.f);
            else
                PthLed(kChompiKeyLed, white, Shift() ? 1.f : 0.f);

            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
                return false;

            const bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
                keys_.Chompi(rising);
                return true;
            case static_cast<uint16_t>(Hardware::SwId::KEY_27):
                keys_.Play(rising);
                return true;
            case static_cast<uint16_t>(Hardware::SwId::KEY_28):
                keys_.Loop(rising);
                return true;
            default:
                break;
            }

            for (size_t fx = 0; fx < kNumFx; fx++)
            {
                if (buttonID == static_cast<uint16_t>(kFxSlots[fx].key))
                {
                    if (rising)
                    {
                        keys_.FxKey();
                        if (Shift())
                            select_flash_.Start(System::GetNow(), kSelectFlashMs);
                    }
                    fx_.KeyPressed(fx, rising, Shift());
                    return true;
                }
            }

            // anything else used with CHOMPI held is a SHIFT combo
            if (rising)
                keys_.Used();

            // short press cycles the pages, hold to check battery level
            if (buttonID == static_cast<uint16_t>(Hardware::SwId::ENC_6_SW))
            {
                if(!rising && !Shift() && System::GetNow() - batt_hold < kBattHoldMs)
                    page_ = (page_ + 1) % kNumPages;

                batt_hold = System::GetNow();
                batt_display = rising;
                return true;
            }
            if (!rising)
                return true;

            // transport press: back to 1x forward
            if (buttonID == ENC_5_SW)
            {
                if (LoopExists())
                {
                    engine_->looper.ResetSpeed();
                    speed_chunk_ = 0.f;
                }
                return true;
            }
            for (size_t slot = 0; slot < kNumSlots; slot++)
            {
                if (buttonID == static_cast<uint16_t>(kSceneKeys[slot]))
                    ScenePressed(slot, Shift());
            }
            for (const SceneModeKey& key : kSceneModeKeys)
            {
                if (buttonID == static_cast<uint16_t>(key.key))
                    scene_ctl_.ModePressed(key.mode);
            }
            for (size_t knob = 0; knob < kNumFxParams; knob++)
            {
                if (buttonID == static_cast<uint16_t>(kFxKnobSwitches[knob]))
                    fx_.KnobPressed(knob, Shift());
            }
            return true;
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            keys_.Used();
            const float detents = Detents(encoderID, turns);
            if (encoderID == kTransportEncoder)
                TransportTurned(turns);
            else if (encoderID < kNumFxParams)
                fx_.KnobTurned(encoderID, detents, Shift());
            else if (encoderID == kVolumeEncoder)
                VolumeTurned(detents);
            else
                return false;
            return true;
        }

        // called every audio block (ui.h)
        void SetSwitchState(bool state)
        {
            engine_->SetHeadphoneDry(!state);
        }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        // PlayKeys' Host
        friend class PlayKeys<NormalPage>;
        inline bool SceneArmed() const { return scene_ctl_.Armed(); }
        inline bool ShiftPressed() { return fx_.ShiftPressed(); }
        inline void ShiftUsed() { fx_.ShiftUsed(); }
        bool FreezeMorph()
        {
            ScopedIrqBlocker irq;
            return scene_ctl_.FreezeMorph();
        }
        inline void Refused() { loop_refused_.Start(System::GetNow()); }
        inline uint32_t Now() const { return System::GetNow(); }
        inline Looper::State LooperState() const { return engine_->looper.GetState(); }
        inline bool CanRecordQuantized() { return engine_->looper.CanRecordQuantized(); }
        inline void StartRecording(bool quantized) { engine_->looper.StartRecording(quantized); }
        inline void StopRecording() { engine_->looper.StopRecording(); }
        inline void TogglePlay() { engine_->looper.TogglePlay(); }

        /** Once per frame, before drawing: what follows from time and the looper's state */
        void Update(uint32_t now)
        {
            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore && now - init_time > 1500)
                init_ignore = false;

            // hold PLAY + LOOP to erase
            if (keys_.EraseDue(now))
            {
                engine_->looper.Erase();
                SetMix(0.f); // the input fades in while the loop fades out
                speed_chunk_ = 0.f;
            }

            // jump to fully wet when a recording closes into playback
            const Looper::State looper_state = engine_->looper.GetState();
            if (last_looper_state_ == Looper::State::RECORDING && looper_state == Looper::State::PLAYING)
            {
                SetMix(1.f);
                speed_chunk_ = 0.f; // a new loop starts at 1x with no detents carried over
            }
            last_looper_state_ = looper_state;

            // a scene confirmed: flash its slot once the card has been written, white if it was
            if (scene_flash_waiting_ && scenes_->GetSaveState() != SceneStore::SaveState::PENDING)
            {
                scene_flash_waiting_ = false;
                scene_flash_ok_ = scenes_->GetSaveState() == SceneStore::SaveState::OK;
                scene_flash_signal_.Start(now);
            }
        }

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

        /** A tempo tap: with a loop, it refits the loop's beats; without one, it sets the
         *  tempo, unless MIDI clock runs (TempoClock.h) */
        void Tap()
        {
            const uint32_t now = System::GetNow();
            if (!engine_->CanTap())
            {
                loop_refused_.Start(now);
                return;
            }
            tap_flash_.Start(now, kTapFlashMs);
            if (tap_tempo_.Tap(now))
                engine_->TapTempo(tap_tempo_.Bpm());
        }

        void SetMix(float mix)
        {
            mix_ = fclamp(mix, 0.f, 1.f);
            engine_->SetMix(mix_);
        }

        void ScenePressed(size_t slot, bool shift)
        {
            SceneControls<PassthroughEngine>::Slot result;
            {
                // a recall within one audio block
                ScopedIrqBlocker irq;
                result = scene_ctl_.Press(slot, shift);
            }
            if (result == SceneControls<PassthroughEngine>::Slot::REFUSED)
                SceneRefusedBlink(slot);
        }

        void ConfirmScene()
        {
            const int slot = scene_ctl_.Confirm();
            if (slot == kNoScene)
                return;
            // the slot flashes once SceneStore::Process has written the card (Update)
            scenes_->RequestSave();
            scene_flash_ = slot;
            scene_flash_waiting_ = true;
            scene_flash_signal_.Stop();
        }

        void SceneRefusedBlink(size_t slot)
        {
            scene_refused_ = static_cast<int>(slot);
            scene_refused_signal_.Start(System::GetNow());
        }

        /** The current scene mode's colour, white without one */
        const float* SceneModeColor() const
        {
            for (const SceneModeKey& key : kSceneModeKeys)
            {
                if (scene_ctl_.Mode() == key.mode)
                    return key.color;
            }
            return white;
        }

        void DrawSceneLeds(uint32_t now)
        {
            const SceneMode mode = scene_ctl_.Mode();
            const float* mode_color = SceneModeColor();
            for (const SceneModeKey& key : kSceneModeKeys)
                SmtLed(key.led, key.color, mode == key.mode ? 1.f : kFxOffLevel);

            const bool blink_on = (now / kSceneBlinkMs) % 2 == 0;
            for (size_t slot = 0; slot < kNumSlots; slot++)
            {
                const int s = static_cast<int>(slot);
                const float* color = white;
                float level = 0.f;
                if (s == scene_flash_ && scene_flash_waiting_)
                    level = 1.f; // writing the card
                else if (s == scene_flash_ && scene_flash_signal_.Active(now))
                {
                    color = scene_flash_ok_ ? white : red;
                    level = scene_flash_signal_.BlinkLit(now) ? 1.f : 0.f;
                }
                else if (s == scene_refused_ && scene_refused_signal_.Active(now))
                {
                    color = red;
                    level = scene_refused_signal_.BlinkLit(now) ? 1.f : 0.f;
                }
                else if (mode != SceneMode::NONE)
                {
                    // in a mode: the source lit, the pick blinking, the slots it can act on
                    // dim, all in the mode's colour; the rest dark
                    color = mode_color;
                    if (s == scene_ctl_.Source())
                        level = 1.f;
                    else if (s == scene_ctl_.Selected())
                        level = blink_on ? 1.f : 0.f;
                    else if (scene_ctl_.Valid(slot))
                        level = kFxOffLevel;
                }
                else if (s == scene_ctl_.Morphing())
                {
                    // morphing to it: blinking on the FX clock's beats
                    const uint32_t pos = engine_->FxClockPosition();
                    level = pos % kPulsesPerBeat < kPulsesPerBeat / 2 ? 1.f : 0.f;
                }
                else if (s == scene_ctl_.Active())
                {
                    // edited: a slow pulse between the saved and the active brightness
                    const float phase = static_cast<float>(now % kScenePulseMs) / kScenePulseMs;
                    level = scene_ctl_.Edited() ? .6f + .4f * cosf(phase * TWOPI_F) : 1.f;
                }
                else if (scenes_->scenes[slot].used)
                    level = kFxOffLevel;
                SmtLed(kSceneLeds[slot], color, level);
            }
        }

        void DrawFxLeds(uint32_t now)
        {
            // knob LEDs: the selected FX's parameters in its colours
            const size_t selected = fx_.Selected();
            const float* const* colors = kFxSlots[selected].knob_colors;
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                const float val = fx_.Param(selected, p);
                if (p >= kFxParams[selected].num_params)
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
                if (fx_.IsOn(fx))
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
                // a select (SHIFT + the key): a white flash
                if (fx == selected && select_flash_.Active(now))
                    level = white = 1.f;
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

            // a tempo tap: a white flash, over whatever LOOP was showing
            if (tap_flash_.Active(now))
                loop[0] = loop[1] = loop[2] = 1.f;

            // refused quantized record or tap: 3 red blinks, over whatever LOOP was showing
            if (loop_refused_.Active(now))
            {
                loop[0] = loop_refused_.BlinkLit(now) ? 1.f : 0.f;
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

        inline bool Shift() const { return keys_.Shift(); }

        // LED helpers: a colour at a level, and a crossfade between two colours
        static void SmtLed(uint8_t led, const float* color, float level)
        {
            SetSmtLedFloat(led, level * color[0], level * color[1], level * color[2]);
        }
        static void PthLed(uint8_t led, const float* color, float level)
        {
            SetPthLedFloat(led, level * color[0], level * color[1], level * color[2]);
        }
        static void Xfade(const float* a, const float* b, float t, float* r, float* g, float* bl)
        {
            *r = color_xfade(a[0], b[0], t);
            *g = color_xfade(a[1], b[1], t);
            *bl = color_xfade(a[2], b[2], t);
        }

        Hardware *hw_;
        PassthroughEngine *engine_;
        SceneStore *scenes_;

        Looper::State last_looper_state_ = Looper::State::EMPTY;

        float out_gain_;
        float in_gain_;
        float final_comp_;
        float mix_;
        uint8_t page_;

        PlayKeys<NormalPage> keys_;
        LedSignal loop_refused_; // a refused quantized record or tap
        TapTempo tap_tempo_;
        LedSignal tap_flash_;
        LedSignal select_flash_; // on the selected FX's key
        float speed_chunk_ = 0.f;   // transport detents towards the next speed step

        FxControls<PassthroughEngine> fx_;
        SceneControls<PassthroughEngine> scene_ctl_;
        int scene_flash_ = kNoScene;   // confirmed, flashing
        LedSignal scene_flash_signal_;
        bool scene_flash_waiting_ = false; // for the card to be written
        bool scene_flash_ok_ = false;  // saved to the card
        int scene_refused_ = kNoScene; // refused, pressed
        LedSignal scene_refused_signal_;

        bool batt_display = false; // VOLUME held
        uint32_t batt_hold = 0;     // when VOLUME was last pressed or released
    };

} // namespace chompi
