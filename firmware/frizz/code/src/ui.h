/** @file ui.h
 *  @brief UI built on libDaisy's daisy::UI page system. Each screen/mode of the UI
 *  (normal play, test mode, boot animation etc.) is a UiPage subclass
 *
 *  Pages are opened and closed here and the UI events get generated and sent to
 *  the active page.
 */
#pragma once
#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "TestPage.h"
#include "BootPage.h"
#include "RainbowWavePage.h"
#include "passthroughEngine.h"
#include "MidiClock.h"

namespace chompi
{
    static const uint8_t encoder_map[6] = {1, 2, 3, 0, 4, 5};

    enum CanvasIds
    {
        canvasLedDisplay = 0,
        NUM_CANVASES
    };

    class UserInterface
    {
    public:
        void Init(PassthroughEngine *engine, Hardware *hw, MidiClock *midi_clock)
        {
            hw_ = hw;
            engine_ = engine;

            /** Describe UI special controls - if any */
            daisy::UI::SpecialControlIds specialControlIds; /**< None here */
            /** Canvas Descriptor */
            daisy::UiCanvasDescriptor ledDisplayDescriptor;
            ledDisplayDescriptor.id_ = canvasLedDisplay;
            ledDisplayDescriptor.handle_ = nullptr;
            ledDisplayDescriptor.updateRateMs_ = 16; /**< 30Hz */
            ledDisplayDescriptor.clearFunction_ = ClearLeds;
            ledDisplayDescriptor.flushFunction_ = FlushLeds;

            /** Init */
            ui.Init(event_queue,
                    specialControlIds,
                    {ledDisplayDescriptor},
                    canvasLedDisplay);

            normal_page_.Init(engine_, hw_, midi_clock);
            ui.OpenPage(normal_page_);

            boot_page_.Init(hw_);
            ui.OpenPage(boot_page_);

            test_page_.Init(hw_);

            // The toggle switch (SW_TOG) is a physical switch, not a
            // momentary button. This samples it at boot and creates
            // an initial press event if it reads down for the majority of samples
            uint16_t state = 0;
            for (int i = 0; i < 512; i++)
            {
                hw->ProcessAllControls();
                daisy::System::DelayUs(500);
                state += !hw_->button_sr.State(static_cast<int>(Hardware::SwId::SW_TOG));
            }

            // state should be debounced
            if (state >= 480)
                event_queue.AddButtonPressed(static_cast<int>(Hardware::SwId::SW_TOG), 1);
        }

        inline bool InTestMode() { return test_page_.IsActive(); }
        void TestMode()
        {
            ui.OpenPage(test_page_);
            normal_page_.SetInitIgnore(false);
        }

        void RainbowWave()
        {
            if(!test_page_.IsActive())
                ui.OpenPage(rainbow_page_);
        }
        inline bool InRainbows() { return rainbow_page_.IsActive(); }

        void StopBootAnimation()
        {
            ui.ClosePage(boot_page_);
        }

        inline bool GetToggleState() { return toggle_state; }

        // Translates raw debounced hardware state into daisy::UiEventQueue events
        // for whatever page is active.
        void GenerateEvents()
        {
            toggle_state = hw_->GetToggleState();

            if(test_page_.IsClosable() && test_page_.IsActive())
            {
                ui.ClosePage(test_page_);
                normal_page_.ResetSmtLeds();
            }

            if(rainbow_page_.IsClosable() && rainbow_page_.IsActive())
            {
                ui.ClosePage(rainbow_page_);
                normal_page_.ResetSmtLeds();
            }

            for (int i = 0; i < static_cast<int>(Hardware::SwId::SR_LAST); i++)
            {
                if (i == ENC_5_SW)
                    continue; // skip this one
                else if(i == static_cast<int>(Hardware::SwId::SW_TOG))
                {
                    // this should be smoothed
                    normal_page_.SetSwitchState(toggle_state);
                    test_page_.SetSwitchState(toggle_state);
                }
                else if (hw_->button_sr.FallingEdge(i))
                    event_queue.AddButtonReleased(i);
                else if (hw_->button_sr.RisingEdge(i))
                    event_queue.AddButtonPressed(i, 1);
            }

            if (hw_->enc[4].FallingEdge())
                event_queue.AddButtonReleased(ENC_5_SW);
            else if (hw_->enc[4].RisingEdge())
                event_queue.AddButtonPressed(ENC_5_SW, 1);

            // encoder_map remaps physical encoder wiring order to logical knob
            // order. Knobs 0 and 4 get finer resolution (1x per detent)
            // while the rest move 3x faster per detent, since those don't need
            // as fine resolution.
            for (int i = 0; i < static_cast<int>(Hardware::EncoderId::ENC_LAST); i++)
            {
                int inc = hw_->enc[i].Increment();
                if (inc == 1 || inc == -1)
                {
                    uint8_t enc = encoder_map[i];

                    if(enc == 0 || enc == 4)
                        event_queue.AddEncoderTurned(enc, inc, 0);
                    else
                        event_queue.AddEncoderTurned(enc, inc * 3, 0);
                }
            }
        }

        void DoEvents() { ui.Process(); }

    inline void TestPowerCable(bool cable) { test_page_.SetPowerCable(cable); }
    inline void TestBMC(bool good) { test_page_.SetBMCGood(good); }

    // public so we can check IsActive from main
    BootPage boot_page_;
    NormalPage normal_page_;
    TestPage test_page_;
    RainbowPage rainbow_page_;
    daisy::UiEventQueue event_queue;
    private:
        daisy::UI ui;
        Hardware *hw_;
        PassthroughEngine *engine_;

        bool toggle_state;
    };

} // namespace chompi
