/** @file BootPage.h
 *  @brief The boot page: a slow glow in random colours while FRIZZ starts up (kBootScreenMs,
 *  chompi_main.cpp).
 */
#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class BootPage : public daisy::UiPage
    {
    public:

        void Init(Hardware*)
        {
            RandomColors();
        }

        void RandomColors()
        {
            r = System::GetNow() % 66;
            g = System::GetNow() % 53;
            b = System::GetNow() % 36;

            r = r / 66.f;
            g = g / 53.f;
            b = b / 36.f;
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            bright += bright_inc;
            if(bright > 1.f)
            {
                bright_inc *= -1.f;
            }
            else if(bright < 0.f)
            {
                RandomColors();
                bright_inc *= -1.f;
            }                

            for(size_t i = 0; i < kNumPthLeds; i++)
            {
                SetPthLedFloat(i, r * bright, g * bright, b * bright);
            }

            for(size_t i = 0; i < kNumSmtLeds; i++)
            {
                SetSmtLedFloat(i, r * bright, g * bright, b * bright);
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            return false; // do nothing
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            return false; // do nothing
        }

    private:
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        float bright = 0.f;
        float bright_inc = .01f;
    };
} // namespace chompi
