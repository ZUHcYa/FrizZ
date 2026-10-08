// Host stand-in for libDaisy's DaisySeed: its pins and its audio
#pragma once
#include "daisy.h"

namespace daisy
{
namespace seed
{
// distinct pins, not the Seed's real ports: board.h only needs to tell them apart
#define TWIN_SEED_PIN(n) constexpr Pin D##n(static_cast<GPIOPort>(n / 16), n % 16)
TWIN_SEED_PIN(0); TWIN_SEED_PIN(1); TWIN_SEED_PIN(2); TWIN_SEED_PIN(3); TWIN_SEED_PIN(4);
TWIN_SEED_PIN(5); TWIN_SEED_PIN(6); TWIN_SEED_PIN(7); TWIN_SEED_PIN(8); TWIN_SEED_PIN(9);
TWIN_SEED_PIN(10); TWIN_SEED_PIN(11); TWIN_SEED_PIN(12); TWIN_SEED_PIN(13); TWIN_SEED_PIN(14);
TWIN_SEED_PIN(15); TWIN_SEED_PIN(16); TWIN_SEED_PIN(17); TWIN_SEED_PIN(18); TWIN_SEED_PIN(19);
TWIN_SEED_PIN(20); TWIN_SEED_PIN(21); TWIN_SEED_PIN(22); TWIN_SEED_PIN(23); TWIN_SEED_PIN(24);
TWIN_SEED_PIN(25); TWIN_SEED_PIN(26); TWIN_SEED_PIN(27); TWIN_SEED_PIN(28); TWIN_SEED_PIN(29);
TWIN_SEED_PIN(30); TWIN_SEED_PIN(31); TWIN_SEED_PIN(32);
#undef TWIN_SEED_PIN
} // namespace seed

class DaisySeed
{
public:
    void Init(bool = false) {}
    float AudioSampleRate() { return 48000.f; }
    size_t AudioBlockSize() { return audio_handle.GetConfig().blocksize; }
    SaiHandle& AudioSaiHandle() { return sai_1_; }
    void StartAudio(AudioHandle::AudioCallback cb) { twin::StartAudio(cb); }
    void SetLed(bool) {}

    AudioHandle audio_handle;

private:
    SaiHandle sai_1_;
};
} // namespace daisy
