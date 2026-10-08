// Host stand-in for libDaisy's daisy_core.h: pins, memory sections, the C GPIO API
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "board.h"

#define DSY_SDRAM_BSS
#define DMA_BUFFER_MEM_SECTION
#define DSY_MIN(in, mn) (in < mn ? in : mn)
#define DSY_MAX(in, mx) (in > mx ? in : mx)
#define DSY_CLAMP(in, mn, mx) (DSY_MAX(DSY_MIN(in, mx), mn))

namespace daisy
{
enum GPIOPort
{
    PORTA,
    PORTB,
    PORTC,
    PORTD,
    PORTE,
    PORTF,
    PORTG,
    PORTH,
    PORTI,
    PORTJ,
    PORTK,
    PORTX,
};

struct Pin
{
    GPIOPort port;
    uint8_t pin;
    constexpr Pin() : port(PORTX), pin(255) {}
    constexpr Pin(GPIOPort p, uint8_t n) : port(p), pin(n) {}
    constexpr bool IsValid() const { return port != PORTX && pin < 16; }
    constexpr int Id() const { return IsValid() ? port * 16 + pin : -1; }
    constexpr bool operator==(const Pin& o) const { return port == o.port && pin == o.pin; }
    constexpr bool operator!=(const Pin& o) const { return !(*this == o); }
};
} // namespace daisy

typedef daisy::Pin dsy_gpio_pin;

typedef enum
{
    DSY_GPIO_MODE_INPUT,
    DSY_GPIO_MODE_OUTPUT_PP,
    DSY_GPIO_MODE_OUTPUT_OD,
    DSY_GPIO_MODE_ANALOG,
} dsy_gpio_mode;

typedef enum
{
    DSY_GPIO_NOPULL,
    DSY_GPIO_PULLUP,
    DSY_GPIO_PULLDOWN,
} dsy_gpio_pull;

typedef struct
{
    dsy_gpio_pin pin;
    dsy_gpio_mode mode;
    dsy_gpio_pull pull;
} dsy_gpio;

inline void dsy_gpio_init(const dsy_gpio*) {}
inline void dsy_gpio_deinit(const dsy_gpio*) {}
// an unconnected pin reads high, as the pulled-up inputs do at rest
inline uint8_t dsy_gpio_read(const dsy_gpio* p)
{
    return p->pin.IsValid() ? twin::PinRead(p->pin.Id()) : 1;
}
inline void dsy_gpio_write(const dsy_gpio* p, uint8_t state)
{
    if (p->pin.IsValid())
        twin::PinWrite(p->pin.Id(), state);
}
inline void dsy_gpio_toggle(const dsy_gpio* p)
{
    if (p->pin.IsValid())
        twin::PinWrite(p->pin.Id(), !twin::PinRead(p->pin.Id()));
}

// the firmware's sample conversions (libDaisy's util/...)
inline float s162f(int32_t x) { return (float)x * (1.f / 32767.f); }
inline int32_t f2s16(float x)
{
    x = x <= -1.f ? -1.f : x;
    x = x >= 1.f ? 1.f : x;
    return (int32_t)(x * 32767.f);
}

// the C names of the ports, as libDaisy's midi.cpp uses them
#define DSY_GPIOA daisy::PORTA
#define DSY_GPIOB daisy::PORTB
#define DSY_GPIOC daisy::PORTC
#define DSY_GPIOD daisy::PORTD
#define DSY_GPIOX daisy::PORTX
