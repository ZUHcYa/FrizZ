/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h). Only the VOLUME knob (encoder 6) does
 *  anything, following TAPE's Volume Engine:
 *   - page 1 (default): output gain for headphone and master out, LED is a VU meter
 *   - page 2 (press):   input gain for the AUX input, LED blue (0%) to red (100%)
 *   - SHIFT + turn:     master compressor amount, LED dark to light blue
 *   - press and hold:   battery level check after 1.25s
 *
 *  SHIFT is the CHOMPI key held with the mode switch DOWN.
 */
#pragma once

#include "hardware.h"
#include "passthroughEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
    static const float kEncoderCoarseStep = .01f;
    static const uint32_t kBattHoldMs = 1250;

    static const float kDefaultOutGain = .75f;
    static const float kDefaultInGain = .75f;

    static const uint8_t kVolumeLed = 9;
    static const uint8_t kChompiKeyLed = 0;

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};

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
            page_ = 0;

            // the engine only hears about a value when it changes, so push all three now
            engine_->SetMainGain(out_gain_);
            engine_->SetInputGain(in_gain_);
            engine_->SetFinalComp(final_comp_);

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
                r = med_blue[0] * (final_comp_ * .9f + .1f);
                g = med_blue[1] * (final_comp_ * .9f + .1f);
                b = med_blue[2] * (final_comp_ * .9f + .1f);
            }
            else if (page_ == 0)
            {
                float vu_sample = engine_->GetVUSample();

                r = out_gain_ * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                g = out_gain_ * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                b = out_gain_ * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
            }
            else
            {
                r = color_xfade(blue[0], red[0], in_gain_);
                g = color_xfade(blue[1], red[1], in_gain_);
                b = color_xfade(blue[2], red[2], in_gain_);
            }
            SetPthLedFloat(kVolumeLed, r, g, b);

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
            // short press toggles the page, hold to check battery level
            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && !Shift() && System::GetNow() - batt_hold < kBattHoldMs)
                    page_ = !page_;

                batt_hold = System::GetNow();
                batt_display = rising;
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
                chompi_key_pressed = rising;
                break;

            default:
                break;
            }

            return true;
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore || encoderID != 5)
                return false;

            const float inc = turns * kEncoderCoarseStep;

            if (Shift())
            {
                final_comp_ = fclamp(final_comp_ + inc, 0.f, 1.f);
                engine_->SetFinalComp(final_comp_);
            }
            else if (page_ == 0)
            {
                out_gain_ = fclamp(out_gain_ + inc, 0.f, 1.f);
                engine_->SetMainGain(out_gain_);
            }
            else
            {
                in_gain_ = fclamp(in_gain_ + inc, 0.f, 1.f);
                engine_->SetInputGain(in_gain_);
            }

            return true;
        }

        inline bool getSwitchState() { return switch_state; }
        void SetSwitchState(bool state) { switch_state = state; }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        // switch_state is true with the mode switch DOWN
        inline bool Shift() { return chompi_key_pressed && switch_state; }

        Hardware *hw_;
        PassthroughEngine *engine_;

        float out_gain_;
        float in_gain_;
        float final_comp_;
        uint8_t page_;

        bool switch_state = false;
        bool chompi_key_pressed = false;

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi
