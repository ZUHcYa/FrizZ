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
 *  SHIFT is the CHOMPI key held with the mode switch DOWN.
 *
 *  Looper keys (LOOPER.md 1.2), PLAY = KEY_27, LOOP = KEY_28:
 *   - empty:          LOOP records unquantized, hold PLAY + press LOOP records quantized
 *                     (refused without MIDI clock)
 *   - recording:      LOOP ends the recording (quantized: at the end of the bar)
 *   - loop exists:    PLAY toggles play / pause, LOOP does nothing,
 *                     hold PLAY + LOOP for 2s erases (either key first)
 *  LOOP acts on press so recording starts and stops exactly then. PLAY acts on release, and
 *  only if LOOP wasn't pressed during the hold, so the PLAY + LOOP combos never also toggle.
 *
 *  TEMPORARY (looper step 1, remove in step 5): the transport LEDs flash on every MIDI
 *  clock beat, green for TRS and blue for USB, brighter every 4th beat.
 */
#pragma once

#include "hardware.h"
#include "passthroughEngine.h"
#include "MidiClock.h"
#include "temp_led_stuff.h"

namespace chompi
{
    static const float kEncoderCoarseStep = .01f;
    static const uint32_t kBattHoldMs = 1250;

    static const float kDefaultOutGain = .75f;
    static const float kDefaultInGain = .75f;
    static const float kDefaultMix = 0.f; // fully dry: the input as before, no looper

    static const uint8_t kNumPages = 3;

    static const uint8_t kVolumeLed = 9;
    static const uint8_t kChompiKeyLed = 0;
    static const uint8_t kTransportLedL = 5;
    static const uint8_t kTransportLedR = 6;
    static const uint32_t kBeatFlashMs = 50;
    static const uint32_t kEraseHoldMs = 2000;

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};
    static const float purple[3] = {.58f, .05f, 1.f};

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(PassthroughEngine *engine, Hardware *hw, MidiClock *midi_clock)
        {
            hw_ = hw;
            engine_ = engine;
            midi_clock_ = midi_clock;

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
                erase_armed_ = false;
            }

            // TEMPORARY beat indicator, see the file comment
            if (midi_clock_->HasClock())
            {
                const uint32_t beat = midi_clock_->GetTicks() / kTicksPerBeat;
                if (beat != last_beat_)
                {
                    last_beat_ = beat;
                    beat_flash_ = now;
                    downbeat_ = beat % 4 == 0;
                }
            }
            if (now - beat_flash_ < kBeatFlashMs)
            {
                const float* color = midi_clock_->GetSource() == MidiClock::Source::USB ? &blue[0] : &green[0];
                const float level = downbeat_ ? 1.f : .25f;
                SetPthLedFloat(kTransportLedL, color[0] * level, color[1] * level, color[2] * level);
                SetPthLedFloat(kTransportLedR, color[0] * level, color[1] * level, color[2] * level);
            }

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
                mix_ = fclamp(mix_ + inc, 0.f, 1.f);
                engine_->SetMix(mix_);
            }
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

        // switch_state is true with the mode switch DOWN
        inline bool Shift() { return chompi_key_pressed && switch_state; }

        Hardware *hw_;
        PassthroughEngine *engine_;
        MidiClock *midi_clock_;

        uint32_t last_beat_ = 0;
        uint32_t beat_flash_ = 0;
        bool downbeat_ = false;

        float out_gain_;
        float in_gain_;
        float final_comp_;
        float mix_;
        uint8_t page_;

        bool switch_state = false;
        bool chompi_key_pressed = false;

        bool play_pressed_ = false;
        bool loop_pressed_ = false;
        bool play_combo_ = false;   // LOOP was pressed during this PLAY hold
        bool erase_armed_ = false;  // PLAY + LOOP held on an existing loop
        uint32_t erase_hold_ = 0;
        uint32_t record_refused_ = 0;

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi
