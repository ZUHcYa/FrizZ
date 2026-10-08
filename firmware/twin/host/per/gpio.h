// Host stand-in for libDaisy's GPIO class, on the twin's pins (board.h)
#pragma once
#include "daisy_core.h"

namespace daisy
{
class GPIO
{
public:
    enum class Mode
    {
        INPUT,
        OUTPUT,
        OPEN_DRAIN,
        ANALOG,
    };
    enum class Pull
    {
        NOPULL,
        PULLUP,
        PULLDOWN,
    };
    enum class Speed
    {
        LOW,
        MEDIUM,
        HIGH,
        VERY_HIGH,
    };

    void Init(Pin p, Mode = Mode::INPUT, Pull = Pull::NOPULL, Speed = Speed::LOW) { pin_ = p; }
    bool Read() { return pin_.IsValid() ? twin::PinRead(pin_.Id()) : true; }
    void Write(bool state)
    {
        if (pin_.IsValid())
            twin::PinWrite(pin_.Id(), state);
    }
    void Toggle() { Write(!Read()); }

private:
    Pin pin_;
};
} // namespace daisy
