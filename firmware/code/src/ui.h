/** @file ui.h
 *  @brief UI built on libDaisy's daisy::UI page system. Each screen of the UI (the play page,
 *  the boot glow, the rainbow) is a UiPage subclass
 *
 *  Pages are opened and closed here and the UI events get generated and sent to
 *  the active page.
 */
#pragma once
#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "BootPage.h"
#include "RainbowWavePage.h"
#include "passthroughEngine.h"
#include "EventLog.h"

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
        void Init(PassthroughEngine *engine, Hardware *hw, SceneStore *scenes, EventLog *log)
        {
            hw_ = hw;
            log_ = log;

            /** Describe UI special controls - if any */
            daisy::UI::SpecialControlIds specialControlIds; /**< None here */
            /** Canvas Descriptor */
            daisy::UiCanvasDescriptor ledDisplayDescriptor;
            ledDisplayDescriptor.id_ = canvasLedDisplay;
            ledDisplayDescriptor.handle_ = nullptr;
            ledDisplayDescriptor.updateRateMs_ = 16; /**< ~60Hz */
            ledDisplayDescriptor.clearFunction_ = ClearLeds;
            ledDisplayDescriptor.flushFunction_ = FlushLeds;

            /** Init */
            ui.Init(event_queue,
                    specialControlIds,
                    {ledDisplayDescriptor},
                    canvasLedDisplay);

            normal_page_.Init(engine, hw_, scenes, log);
            ui.OpenPage(normal_page_);

            boot_page_.Init();
            ui.OpenPage(boot_page_);
        }

        void RainbowWave()
        {
            ui.OpenPage(rainbow_page_);
        }
        inline bool InRainbows() { return rainbow_page_.IsActive(); }

        void StopBootAnimation()
        {
            ui.ClosePage(boot_page_);
        }

        // Translates raw debounced hardware state into daisy::UiEventQueue events
        // for whatever page is active, and logs each for a bug report (EventLog.h)
        void GenerateEvents()
        {
            for (int i = 0; i < static_cast<int>(Hardware::SwId::SR_LAST); i++)
            {
                // the transport's switch comes from its encoder below; the mode switch isn't a
                // key and nothing uses it yet (hw_->GetToggleState() reads it)
                if (i == ENC_5_SW || i == static_cast<int>(Hardware::SwId::SW_TOG))
                    continue;
                else if (hw_->button_sr.FallingEdge(i))
                    Released(i);
                else if (hw_->button_sr.RisingEdge(i))
                    Pressed(i);
            }

            if (hw_->enc[4].FallingEdge())
                Released(ENC_5_SW);
            else if (hw_->enc[4].RisingEdge())
                Pressed(ENC_5_SW);
            log_->Toggle(hw_->GetToggleState());

            // encoder_map remaps the encoders' wiring order to the knobs' order; one event per
            // detent
            for (int i = 0; i < static_cast<int>(Hardware::EncoderId::ENC_LAST); i++)
            {
                int inc = hw_->enc[i].Increment();
                if (inc == 1 || inc == -1)
                {
                    event_queue.AddEncoderTurned(encoder_map[i], inc, 0);
                    log_->Add(EventLog::TURN, i + 1, inc);
                }
            }
        }

        /** Closes the finished rainbow page, then dispatches the events and draws. Runs in
         *  one context at a time (chompi_main.cpp), so the page stack is only changed here;
         *  GenerateEvents only adds to the IRQ-safe event queue */
        void DoEvents()
        {
            if(rainbow_page_.IsClosable() && rainbow_page_.IsActive())
            {
                ui.ClosePage(rainbow_page_);
                normal_page_.ResetSmtLeds();
            }

            ui.Process();
        }

    private:
        void Pressed(int key)
        {
            event_queue.AddButtonPressed(key, 1);
            log_->Add(EventLog::KEY, key, 1);
        }
        void Released(int key)
        {
            event_queue.AddButtonReleased(key);
            log_->Add(EventLog::KEY, key, 0);
        }

        BootPage boot_page_;
        NormalPage normal_page_;
        RainbowPage rainbow_page_;
        daisy::UiEventQueue event_queue;
        daisy::UI ui;
        Hardware *hw_;
        EventLog *log_;
    };

} // namespace chompi
