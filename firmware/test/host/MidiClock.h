// Host stand-in for FRIZZ's MidiClock: no clock, so TempoClock runs on its internal 120 BPM
#pragma once
#include "daisy.h"
namespace chompi
{
static const uint32_t kTicksPerBeat = 24;
static const uint32_t kTicksPerBar = kTicksPerBeat * 4;
class MidiClock
{
public:
    inline bool HasClock() const { return false; }
    inline uint32_t GetTicks() const { return 0; }
    inline uint32_t GetLastTickTime() const { return 0; }
    inline float GetTickPeriod() const { return 0.f; }
    inline float GetBpm() const { return 0.f; }
};
}
