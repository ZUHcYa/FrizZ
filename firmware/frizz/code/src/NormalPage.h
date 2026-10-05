/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h).
 *
 *  VOLUME knob (encoder 6), following TAPE's Volume Engine:
 *   - page 1 (default): output gain for headphone and master out, LED is a VU meter
 *   - page 2 (press):   input gain for the AUX input, LED blue (0%) to red (100%)
 *   - page 3 (press):   master compressor amount, LED dark to light blue
 *   - SHIFT + turn:     dry/wet mix, LED green (dry, input only) to purple (wet, looper only)
 *   - press and hold:   battery level check after 1.25s
 *
 *  Pressing again on page 3 goes back to page 1.
 *
 *  SHIFT is the CHOMPI key held, in either position of the mode switch. The switch does nothing
 *  in play mode for now; its state is still tracked (switch_state) for later use.
 *
 *  Looper keys (LOOPER.md 1.2), PLAY = KEY_27, LOOP = KEY_28:
 *   - empty:          LOOP records unquantized, hold PLAY + press LOOP records quantized
 *                     (refused without MIDI clock)
 *   - recording:      LOOP ends the recording (quantized: at the end of the bar)
 *   - loop exists:    PLAY toggles play / pause, LOOP does nothing,
 *                     hold PLAY + LOOP for 2s erases (either key first)
 *  Transport knob (encoder 5, LOOPER.md 1.5), once a loop exists:
 *   - turn while playing: speed in 5ths and octaves, 4 detents per step, reverse past 1/16x
 *   - turn while paused:  scrub
 *   - press:              back to 1x forward
 *   - SHIFT + turn:       nothing
 *
 *  LOOP acts on press so recording starts and stops exactly then. PLAY acts on release, and
 *  only if LOOP wasn't pressed during the hold, so the PLAY + LOOP combos never also toggle.
 *
 *  The mix jumps to fully wet when a recording closes and back to fully dry on erase.
 *
 *  Looper LEDs (LOOPER.md 1.6): LOOP red while recording, blinking while a quantized recording
 *  finishes its bar, 3 fast red blinks when quantized recording is refused. While a loop
 *  plays, PLAY and LOOP crossfade in white to show the position, dimmed when paused. The
 *  transport LEDs show the speed in TAPE's colours, or the scrub speed while paused.
 *
 *  Punch-in FX (FxChain.h) on the white keys, which key is which in FxSlots.h:
 *   - hold the key:          the effect is on while held
 *   - SHIFT + key:           toggles the latch, the effect stays on after release
 *   - key on a latched FX:   clears the latch, the effect stays on until the key is released
 *   - knobs 1-4 (enc 0-3):   the parameters of the most recently pressed FX key, 1% per
 *                            detent; stepped parameters move one step per
 *                            kFxDetentsPerStep detents; knobs past the FX's num_params do
 *                            nothing
 *   - press knobs 1-4:       resets that parameter to its default
 *   - SHIFT + knobs 1-4:     nothing, turned or pressed (reserved for a second parameter page)
 *  FX key LEDs: off, dimly lit in the FX's colour; on, at full brightness, the audio coming
 *  out of it (FxChain's meters, in dB) pushing the colour towards white. Sends follow their
 *  returns, so after release their keys glow with the tail, fading from full to off. The knob LEDs show the parameter values in the FX's colours,
 *  unused knobs dark.
 */
#pragma once

#include "FxSlots.h"
#include "hardware.h"
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

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(PassthroughEngine *engine, Hardware *hw)
        {
            hw_ = hw;
            engine_ = engine;

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

            // CHOMPI key lights white while it is acting as SHIFT
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

            if (encoderID == 4)
            {
                TransportTurned(turns);
                return true;
            }

            if (encoderID < kNumFxParams)
            {
                FxKnobTurned(encoderID, turns);
                return true;
            }

            if (encoderID != 5)
                return false;

            // ui.h sends the VOLUME knob 3x per detent
            const float inc = turns / 3.f * kVolumeStep;

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

            return true;
        }

        /** System::GetNow() of the last refused quantized record, 0 if none (for the LEDs) */
        inline uint32_t GetRecordRefusedTime() const { return record_refused_; }

        inline bool getSwitchState() { return switch_state; }
        void SetSwitchState(bool state) { switch_state = state; }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
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
                fx_latched_[fx] = Shift() ? !fx_latched_[fx] : false;
            }
            fx_held_[fx] = rising;
            engine_->SetFxOn(fx, fx_held_[fx] || fx_latched_[fx]);
        }

        void FxKnobTurned(uint16_t knob, int16_t turns)
        {
            if (Shift() || knob >= kFxSlots[fx_selected_].num_params)
                return;

            // knob 1 gets 1x per detent from ui.h, the others 3x
            const float detents = knob == 0 ? turns : turns / 3.f;
            float& val = fx_params_[fx_selected_][knob];

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

        void FxKnobPressed(size_t knob)
        {
            if (Shift() || knob >= kFxSlots[fx_selected_].num_params)
                return;

            SetFxParam(fx_selected_, knob, kFxSlots[fx_selected_].defaults[knob]);
            fx_step_chunk_[knob] = 0.f;
        }

        void SetFxParam(size_t fx, size_t param, float val)
        {
            fx_params_[fx][param] = fclamp(val, 0.f, 1.f);
            engine_->SetFxParam(fx, param, fx_params_[fx][param]);
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
        float fx_step_chunk_[kNumFxParams] = {}; // detents towards the next step, stepped params

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi
