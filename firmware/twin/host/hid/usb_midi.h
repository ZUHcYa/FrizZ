// Host stand-in for libDaisy's MidiUsbTransport: what twin::MidiUsb() queues arrives here as the
// raw MIDI bytes the real transport unpacks from its USB-MIDI packets (board.h)
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
    void StartRx(MidiRxParseCallback rx, void* context)
    {
        active_ = true;
        twin::UsbMidiListen(rx, context);
    }
    bool RxActive() { return active_; }
    void FlushRx() {}
    bool Tx(uint8_t*, size_t) { return true; }

private:
    bool active_ = false;
};
} // namespace daisy
