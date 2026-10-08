// Host stand-in for libDaisy's System: the clock is the twin's (board.h)
#pragma once
#include "daisy_core.h"

namespace daisy
{
class System
{
public:
    enum BootloaderMode
    {
        STM = 0,
        DAISY,
        DAISY_SKIP_TIMEOUT
    };

    static uint32_t GetNow() { return static_cast<uint32_t>(twin::NowNs() / 1000000); }
    static uint32_t GetUs() { return static_cast<uint32_t>(twin::NowNs() / 1000); }
    static uint32_t GetTick() { return static_cast<uint32_t>(twin::NowNs() / 5); } // 200 MHz
    static void Delay(uint32_t ms) { twin::DelayNs(uint64_t(ms) * 1000000); }
    static void DelayUs(uint32_t us) { twin::DelayNs(uint64_t(us) * 1000); }
    static void DelayTicks(uint32_t ticks) { twin::DelayNs(uint64_t(ticks) * 5); }
    static uint32_t GetSysClkFreq() { return 480000000; }
    static uint32_t GetHClkFreq() { return 240000000; }
    static uint32_t GetPClk1Freq() { return 120000000; }
    static uint32_t GetPClk2Freq() { return 120000000; }
    static uint32_t GetTickFreq() { return 200000000; }
    static void ResetToBootloader(BootloaderMode = STM) {}
};
} // namespace daisy

// hardware.h's low-battery sleep
#define PWR_LOWPOWERREGULATOR_ON 1
#define PWR_STOPENTRY_WFI 1
inline void HAL_PWR_EnterSTOPMode(uint32_t, uint8_t) { twin::Stop(); }
