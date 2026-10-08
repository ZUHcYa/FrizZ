// Host stand-in for libDaisy's MidiUsbTransport: the twin feeds MIDI through the TRS jack's
// UART only, so USB MIDI never receives anything
#pragma once
#include "daisy_core.h"

namespace daisy
{
class MidiUsbTransport
{
public:
    typedef void (*MidiRxParseCallback)(uint8_t* data, size_t size, void* context);

    struct Config
    {
        enum Periph
        {
            INTERNAL = 0,
            EXTERNAL
        };
        Periph periph;
        uint8_t tx_retry_count;
        Config() : periph(INTERNAL), tx_retry_count(3) {}
    };

    void Init(Config) {}
    void Reset() {}
    void StartRx(MidiRxParseCallback, void*) { active_ = true; }
    bool RxActive() { return active_; }
    void FlushRx() {}
    bool Tx(uint8_t*, size_t) { return true; }

private:
    bool active_ = false;
};
} // namespace daisy
