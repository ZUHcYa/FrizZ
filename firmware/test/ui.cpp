// ui.cpp: plays keys, knobs and MIDI into the virtual CHOMPI (twin/twin.h), FRIZZ's whole
// firmware from power-on with its play page, LEDs and debouncing, and checks what the LEDs show
// and what comes out. Each case boots its own device (a forked process). Run by unit.sh ui.
#include <sys/wait.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include "check.h"
#include "twin.h"

using namespace twin;

// the play page's LEDs (NormalPage.h, FxSlots.h)
static const int kKnob1Led = 1, kPlayLed = 7, kLoopLed = 8;
static const int kFilterKeyLed = 20, kShifterKeyLed = 23;
static const int kVolumeEncoder = 6, kKnob1Encoder = 4; // SW6, SW4

static bool sine = true;
static float phase = 0.f;

/** Runs ms with a 220 Hz sine (or silence) into AUX; the master out's RMS over the time */
static float RunMs(uint32_t ms)
{
    float in[kBlockSize * kChannels] = {}, out[kBlockSize * kChannels];
    double sum = 0.;
    for (uint32_t b = 0; b < ms * 2; b++)
    {
        for (size_t i = 0; i < kBlockSize; i++)
        {
            const float s = sine ? .3f * sinf(phase) : 0.f;
            phase = fmodf(phase + 2.f * float(M_PI) * 220.f / kSampleRate, 2.f * float(M_PI));
            in[i * kChannels + 2] = in[i * kChannels + 3] = s;
        }
        Run(1, in, out);
        for (size_t i = 0; i < kBlockSize; i++)
            sum += out[i * kChannels + 2] * out[i * kChannels + 2];
    }
    return ms ? sqrtf(sum / (ms * 2 * kBlockSize)) : 0.f;
}

static void Tap(const char* key, uint32_t ms = 60)
{
    Press(key, true);
    RunMs(ms);
    Press(key, false);
}

static int Max(const Rgb& c) { return std::max(c.r, std::max(c.g, c.b)); }
static bool Same(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// boot, the rainbow, and the play page ready
static const uint32_t kReadyMs = 6000;

static std::vector<std::pair<const char*, std::function<void()>>> cases;

int main()
{
    cases.push_back({"boot", [] {
        const float booting = RunMs(1000);
        Check(booting == 0.f, "boot: the outputs stay muted while the boot animation runs");
        RunMs(kReadyMs - 1000);
        Check(RunMs(200) > .05f, "boot: then the input reaches the master out");
        bool all_dim = true;
        for (int led : {24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 13, 12})
            all_dim &= Max(SmtLedFull(led)) > 0 && Max(SmtLedFull(led)) < 80;
        Check(all_dim, "boot: every FX key glows dimly in its colour");
    }});

    cases.push_back({"hold", [] {
        RunMs(kReadyMs);
        const float dry = RunMs(300);
        const int off = Max(SmtLedFull(kFilterKeyLed));
        Press("KEY_5", true);
        RunMs(100);
        Check(Max(SmtLedFull(kFilterKeyLed)) > 2 * off, "hold: the filter's key lights up while held");
        Turn(kKnob1Encoder, -40);
        RunMs(500);
        Check(RunMs(300) < dry * .5f, "hold: knob 1 closes the filter while its key is held");
        Press("KEY_5", false);
        RunMs(200);
        Check(fabsf(RunMs(300) - dry) < dry * .05f, "hold: released, the filter is off again");
        Check(Max(SmtLedFull(kFilterKeyLed)) == off, "hold: and its key back to dim");
    }});

    cases.push_back({"latch", [] {
        RunMs(kReadyMs);
        const float dry = RunMs(300);
        Press("KEY_5", true);
        RunMs(100);
        Turn(kKnob1Encoder, -40);
        RunMs(500);
        Tap("KEY_26"); // SHIFT while the key is held: latch
        Press("KEY_5", false);
        RunMs(300);
        Check(RunMs(300) < dry * .5f, "latch: hold the filter, then SHIFT: it stays on");
        Tap("KEY_5");
        RunMs(300);
        Check(fabsf(RunMs(300) - dry) < dry * .05f, "latch: a tap turns it off");
    }});

    cases.push_back({"select", [] {
        RunMs(kReadyMs);
        const float dry = RunMs(300);
        Tap("KEY_5"); // the filter on the knobs
        RunMs(200);
        Rgb filter_knobs[4];
        for (int k = 0; k < 4; k++)
            filter_knobs[k] = PthLedFull(kKnob1Led + k);
        Press("KEY_26", true);
        RunMs(100);
        Tap("KEY_2"); // SHIFT, then the shifter: picked without hearing it
        Press("KEY_26", false);
        RunMs(200);
        bool changed = false;
        for (int k = 0; k < 4; k++)
            changed |= !Same(PthLedFull(kKnob1Led + k), filter_knobs[k]);
        Check(changed, "select: SHIFT + a key puts its effect on the knobs");
        Check(fabsf(RunMs(300) - dry) < dry * .05f, "select: without switching it on");
        Check(Max(SmtLedFull(kShifterKeyLed)) < 80, "select: its key stays dim");
    }});

    cases.push_back({"looper", [] {
        RunMs(kReadyMs);
        Tap("KEY_28");
        RunMs(100);
        const Rgb rec = PthLedFull(kLoopLed);
        Check(rec.r > 100 && rec.g == 0 && rec.b == 0, "looper: LOOP records, lit red");
        RunMs(2000);
        Tap("KEY_28");
        sine = false; // the loop alone
        RunMs(300);
        Check(RunMs(1000) > .05f, "looper: LOOP again plays the recording back");
        Check(Max(PthLedFull(kPlayLed)) + Max(PthLedFull(kLoopLed)) > 0, "looper: PLAY and LOOP show where it is");
        Tap("KEY_27");
        RunMs(300);
        Check(RunMs(300) < .001f, "looper: PLAY pauses it");
        Tap("KEY_28");
        RunMs(300);
        Check(Max(PthLedFull(kLoopLed)) == 0 && Max(PthLedFull(kPlayLed)) == 0, "looper: LOOP erases it");
    }});

    cases.push_back({"quantize", [] {
        RunMs(kReadyMs);
        Press("KEY_27", true);
        RunMs(100);
        Tap("KEY_28"); // PLAY + LOOP without a clock: refused, LOOP blinks red
        Press("KEY_27", false);
        int red = 0;
        for (int i = 0; i < 40; i++)
        {
            RunMs(25);
            red += PthLedFull(kLoopLed).r > 100;
        }
        RunMs(1000);
        Check(red > 0 && Max(PthLedFull(kLoopLed)) == 0, "quantize: without MIDI clock, PLAY + LOOP blinks red and doesn't record");
        // 120 BPM: 24 ticks a beat, one every 20.8 ms
        for (int i = 0; i < 200; i++)
        {
            Midi(0xF8);
            RunMs(i % 6 == 5 ? 20 : 21);
        }
        Press("KEY_27", true);
        RunMs(100);
        Tap("KEY_28");
        Press("KEY_27", false);
        RunMs(100);
        Check(PthLedFull(kLoopLed).r > 100, "quantize: with MIDI clock, PLAY + LOOP records");
    }});

    cases.push_back({"volume", [] {
        RunMs(kReadyMs);
        const float before = RunMs(300);
        Turn(kVolumeEncoder, -30);
        RunMs(500);
        Check(RunMs(300) < before * .7f, "volume: turning VOLUME down lowers the master out");
    }});

    cases.push_back({"shipping", [] {
        Press("KEY_26", true);
        Press("KEY_27", true);
        Press("KEY_28", true);
        RunMs(3000);
        Check(!Powered(), "shipping: CHOMPI, PLAY and LOOP held at power-on switch it off");
    }});

    cases.push_back({"battery", [] {
        RunMs(kReadyMs);
        SetBattery(2.9f, false);
        RunMs(1000);
        bool amber = true;
        for (int i = 0; i < kNumPthLeds; i++)
            amber &= PthLedFull(i).r > 100 || Max(PthLedFull(i)) == 0;
        Check(Powered() && amber, "battery: below 3V, every panel LED flashes amber");
        RunMs(16000);
        Check(!Powered(), "battery: 15 s later it switches itself off");
    }});

    cases.push_back({"charging", [] {
        RunMs(kReadyMs);
        SetBattery(2.9f, true);
        RunMs(20000);
        Check(Powered() && RunMs(300) > .05f, "charging: below 3V on the charger, it keeps playing");
    }});

    int failed = 0;
    for (auto& c : cases)
    {
        fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            Boot();
            c.second();
            fflush(stdout);
            _exit(failures > 255 ? 255 : failures);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status))
        {
            printf("FAIL  %s: crashed\n", c.first);
            failed++;
        }
        else
            failed += WEXITSTATUS(status);
    }
    failures = failed;
    return Finish();
}
