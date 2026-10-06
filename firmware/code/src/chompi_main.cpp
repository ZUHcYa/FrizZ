/** @file chompi_main.cpp
 *  @brief Firmware entry point
 *
 *  CHOMPI (built on the Daisy Seed / STM32H7) has two places code runs, in
 *  order of priority:
 *   1. AudioCallback() - the audio ISR. Runs once per audio block (~24 samples
 *      at 48kHz here). Polls the controls and MIDI clock, and passes the AUX input
 *      through the engine.
 *   2. MainLoop() - Lowest priority, handles UI dispatch, battery checks, writing the FX
 *      scenes to the SD card, and boot-time stuff.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "ui.h"
#include "daisysp.h"
#include "fatfs.h"
#include "passthroughEngine.h"
#include "MidiClock.h"
#include "SceneStore.h"

using namespace daisy;
using namespace chompi;

// How long the boot animation runs (and the outputs stay muted) before audio starts
static const uint32_t kBootScreenMs = 1250;

Hardware hw;
UserInterface ui;

// The SD card holds the FX scenes (SceneStore.h); the hardware self-test (TestPage) uses it too
SdmmcHandler sdmmc;
FatFSInterface fsi;
PassthroughEngine engine;
MidiClock midi_clock;
SceneStore scene_store;

int16_t DSY_SDRAM_BSS loop_mem[kLoopMemSize];

// TEMPO's delay buffers: 10s of interleaved stereo float each, the live and the frozen one
static const size_t kDelayFrames = 480000;
float DSY_SDRAM_BSS delay_mem[kDelayFrames * 2];
float DSY_SDRAM_BSS delay_frozen_mem[kDelayFrames * 2];

// the freezer's buffers (FxFreezer.h): 1 bar at the slowest tempo (4.8s at 50 BPM), plus the
// stereo offset and the seam crossfade, per channel
static const size_t kFreezerFrames = 240000;
float DSY_SDRAM_BSS freezer_mem_l[kFreezerFrames];
float DSY_SDRAM_BSS freezer_mem_r[kFreezerFrames];

// the reverb carries its 64KB buffer, in the fast DTCMRAM like WAVE's. Not zeroed at startup:
// Reverb::Init clears it
#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))
daisysp::Reverb DSY_DTCMRAM_BSS reverb;

// Running count of audio samples since audio started, the time base for MIDI clock ticks
uint32_t sample_clock = 0;

daisysp::Oscillator osc;

// Set just before main() enters its loop. Until then main() blocks in its setup and the audio
// callback runs the UI (the boot animation); from then on only MainLoop does, so the UI is
// never re-entered from the interrupt
volatile bool main_loop_running = false;

bool booting = true;
bool rainbow_done = false;
bool loading_screen = true;

/** Clears the Daisy Seed's 64MB external SDRAM at boot. The loop and delay buffers live
 *  there, and unlike internal-RAM statics they aren't zeroed by the startup code */
void ZeroSDRAM()
{
    uint32_t *beg, *end;
    size_t    size_in_words = (1024 * 1024 * 64) / sizeof(uint32_t);
    beg                     = (uint32_t*)0xc0000000;
    end                     = (uint32_t*)(beg + size_in_words);
    std::fill(beg, end, 0);
}

/** breakdown:
 *  Inputs:
 *  Channel 1 - Microphone (unused)
 *  Channel 2 - X
 *  Channel 3 - Aux L
 *  Channel 4 - Aux R
 *
 *  Outputs:
 *  Channel 1 - Headphone L
 *  Channel 2 - Headphone R
 *  Channel 3 - Master L
 *  Channel 4 - Master R
 */

// The audio ISR. Called by the Daisy audio driver once per block
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    midi_clock.Process(sample_clock);
    sample_clock += size;

    hw.ProcessAllControls();
    ui.GenerateEvents();

    if((booting || loading_screen) && !ui.InTestMode())
    {
        if(!main_loop_running)
            ui.DoEvents();

        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        }

        return;
    }

    if(ui.InTestMode() && ui.GetToggleState())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = osc.Process();
        }
    }
    else {
        engine.Process(in, out, size);
    }
}

uint32_t uit, now, boot_start;

#if !NO_BATT
uint32_t batt;
#endif

void MainLoop(void* data)
{
    if(booting)
    {
        hw.LowBatteryLockoutCheck();
        booting = false;
    }
    else if(!rainbow_done && !loading_screen)
    {
        ui.StopBootAnimation();
        ui.RainbowWave();
        rainbow_done = true;
    }

    now = daisy::System::GetNow();

    if (now - uit > 1)
    {
        ui.DoEvents();
        uit = now;
    }

    // a scene saved, copied or deleted: the card is written here, never in the audio callback
    scene_store.Process();

    if (loading_screen && now - boot_start > kBootScreenMs)
        loading_screen = false;

    #if !NO_BATT

    if(ui.InRainbows())
    {
        batt = now;
    }
    else if(now - batt > 20)
    {
        hw.LowBatteryLockoutCheck();
        batt = now;
    }

    if(ui.InTestMode())
    {
        hw.MpReadAll();

        while (!hw.read_ready) {
            System::Delay(1);
        }
        ui.TestPowerCable(hw.mp_buff_[1] >> 5 & 1); //VIN_RDY

        // Normal NTC_MISSING, BATT_MISSING, NTC1_FAULT, and NTC2_FAULT
        ui.TestBMC(hw.mp_buff_[3] == 0);
    }
    #endif

    System::DelayUs(10);
}

int main(void)
{
    hw.Init();

    midi_clock.Init(hw.seed.AudioSampleRate());

    hw.MpWrite(0x0c, 0B01010001); // set BATT_LOW to 3V, turn on

    hw.MpReadAll();

    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    /** SDMMC Init */
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    const bool card_ok = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
    // FRIZZ's files live in /FRIZZ when the card has that folder, so it can share a card
    // with other firmwares (the launcher at github.com/sfaber02/CHOMPI gives each its own
    // folder), and in the root on a card without it. Every path FRIZZ opens is relative
    if(card_ok)
        f_chdir("/FRIZZ");
    scene_store.Init(card_ok);

    engine.Init(hw.seed.AudioSampleRate(), loop_mem, &midi_clock,
                delay_mem, delay_frozen_mem, kDelayFrames, &reverb,
                freezer_mem_l, freezer_mem_r, kFreezerFrames);

    LedSetup();
    ui.Init(&engine, &hw, &scene_store);

    osc.Init(hw.seed.AudioSampleRate());
    osc.SetAmp(.2f);

    hw.StartAudio(AudioCallback);

    // safe while audio runs: the engine (looper, delay) doesn't run until booting is done
    ZeroSDRAM();

    now = daisy::System::GetNow();
    uit = now;
    boot_start = now;

    #if !NO_BATT
    batt = now;
    #endif

    // get any junk out of the SRs, takes .5s
    uint32_t vol_state = 0;
    uint32_t sleep_state = 0;

    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        vol_state += hw.button_sr.State(int(Hardware::SwId::ENC_6_SW));
        sleep_state += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_28));

        System::DelayUs(100);
    }

    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // SHIPPING MODE
    else if(vol_state > 4000)
        ui.TestMode();

    hw.usb_sw.Write(false);     // give USB control
    daisy::System::Delay(1); // Wait a sec
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    daisy::System::Delay(1); // Wait a sec
    hw.usb_sw.Write(true);     // take USB control

    main_loop_running = true;
    while (1)
    {
        MainLoop(nullptr);
    }
}
