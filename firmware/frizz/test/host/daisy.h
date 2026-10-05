// Host stand-in for libDaisy: just enough for the FRIZZ engine headers to compile on a PC
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#define DSY_SDRAM_BSS
namespace daisy {}
inline float s162f(int32_t x) { return (float)x * (1.f / 32767.f); }
inline int32_t f2s16(float x)
{
    x = x <= -1.f ? -1.f : x;
    x = x >= 1.f ? 1.f : x;
    return (int32_t)(x * 32767.f);
}
