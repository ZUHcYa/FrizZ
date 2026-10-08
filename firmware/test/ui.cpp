// ui.cpp: plays keys, knobs and MIDI into the virtual CHOMPI (twin/twin.h), FRIZZ's whole
// firmware from power-on with its play page, LEDs and debouncing, and checks what the LEDs show
// and what comes out. Each case boots its own device (a forked process). Run by unit.sh ui.
#include <sys/wait.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "check.h"
#include "script.h"
#include "twin.h"

using namespace twin;

// the play page's LEDs (NormalPage.h, FxSlots.h)
static const int kKnob1Led = 1, kPlayLed = 7, kLoopLed = 8;
static const int kFilterKeyLed = 20, kShifterKeyLed = 23;
static const int kVolumeEncoder = 6, kKnob1Encoder = 4; // SW6, SW4
static const int kTransportRevLed = 5, kTransportFwdLed = 6, kVolumeLed = 9;
static const int kSlot1Led = 1, kCompKeyLed = 10, kTapeStopKeyLed = 15;

static bool sine = true;
static float amp = .3f, freq = 220.f, phase = 0.f;
static float clock_bpm = 0.f; // a MIDI clock into the jack while > 0
static double clock_next = 0.;
static float last_out[2] = {0.f, 0.f}, max_step = 0.f; // the master out's largest step, L or R

/** Runs ms with a sine (or silence) into AUX; the master out's RMS over the time */
static float RunMs(uint32_t ms)
{
    float in[kBlockSize * kChannels] = {}, out[kBlockSize * kChannels];
    double sum = 0.;
    for (uint32_t b = 0; b < ms * 2; b++)
    {
        while (clock_bpm > 0.f && clock_next <= NowMs())
        {
            Midi(0xF8);
            clock_next += 60000. / (clock_bpm * 24.);
        }
        for (size_t i = 0; i < kBlockSize; i++)
        {
            const float s = sine ? amp * sinf(phase) : 0.f;
            phase = fmodf(phase + 2.f * float(M_PI) * freq / kSampleRate, 2.f * float(M_PI));
            in[i * kChannels + 2] = in[i * kChannels + 3] = s;
        }
        Run(1, in, out);
        for (size_t i = 0; i < kBlockSize; i++)
        {
            const float o = out[i * kChannels + 2];
            sum += o * o;
            for (int c = 0; c < 2; c++)
            {
                const float v = out[i * kChannels + 2 + c];
                max_step = std::max(max_step, fabsf(v - last_out[c]));
                last_out[c] = v;
            }
        }
    }
    return ms ? sqrtf(sum / (ms * 2 * kBlockSize)) : 0.f;
}

/** The largest step between two samples of the master out over ms */
static float StepMs(uint32_t ms)
{
    max_step = 0.f;
    RunMs(ms);
    return max_step;
}

/** An LED's brightness (its brightest channel, at full scale) every ms for ms */
static std::vector<int> Watch(bool pth, int led, uint32_t ms)
{
    std::vector<int> seen;
    for (uint32_t t = 0; t < ms; t++)
    {
        RunMs(1);
        const Rgb c = pth ? PthLedFull(led) : SmtLedFull(led);
        seen.push_back(std::max(c.r, std::max(c.g, c.b)));
    }
    return seen;
}

/** How long an LED stays lit or dark at a time, in ms: the runs of Watch() between changes,
 *  without the first and the last (cut off by the watch) */
static std::vector<int> Runs(const std::vector<int>& seen, int lit_above)
{
    std::vector<int> runs;
    int len = 0;
    for (size_t i = 1; i < seen.size(); i++)
    {
        len++;
        if ((seen[i] > lit_above) != (seen[i - 1] > lit_above))
        {
            runs.push_back(len);
            len = 0;
        }
    }
    if (!runs.empty())
        runs.erase(runs.begin());
    return runs;
}

// The card across power cycles: a case can leave its card for the next (the cases run in order)
static std::string card_file;
static void KeepCard()
{
    std::ofstream f(card_file, std::ios::binary);
    for (auto& c : CardFiles())
        f << c.first << '\n' << c.second.size() << '\n' << c.second;
}
static void TakeCard()
{
    std::ifstream f(card_file, std::ios::binary);
    std::string path, len;
    while (std::getline(f, path) && std::getline(f, len))
    {
        std::string body(std::stoul(len), '\0');
        f.read(&body[0], body.size());
        CardFiles()[path] = body;
    }
}
static std::string Card(const char* path)
{
    auto it = CardFiles().find(path);
    return it == CardFiles().end() ? "" : it->second;
}
// a scene file's latch for an effect in a slot ("scene 2", "filter"): -1 if it isn't there
static int SavedLatch(const std::string& file, int slot, const char* fx)
{
    std::istringstream in(file);
    std::string line, want = "scene " + std::to_string(slot);
    bool in_slot = false;
    while (std::getline(in, line))
    {
        if (line.rfind("scene ", 0) == 0)
            in_slot = line == want;
        else if (in_slot && line.rfind(std::string(fx) + " ", 0) == 0)
            return line[strlen(fx) + 1] - '0';
    }
    return -1;
}

static void Tap(const char* key, uint32_t ms = 60)
{
    Press(key, true);
    RunMs(ms);
    Press(key, false);
}

static int Max(const Rgb& c) { return std::max(c.r, std::max(c.g, c.b)); }

/** Holds an FX key and taps SHIFT: latched once the key is let go */
static void Latch(const char* key)
{
    Press(key, true);
    RunMs(80);
    Tap("KEY_26");
    Press(key, false);
    RunMs(200);
}

/** SAVE, a slot (1-4), CHOMPI to confirm */
static void Save(int slot)
{
    Tap("KEY_25");
    RunMs(100);
    Tap(("KEY_" + std::to_string(16 + slot)).c_str());
    RunMs(300);
    Tap("KEY_26");
    RunMs(300);
}
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

    // ---- PR #7's hardware checklist, as far as it isn't about the CPU ----

    cases.push_back({"save-latch", [] {
        RunMs(kReadyMs);
        Tap("KEY_25"); // SAVE, slot 1 picked: CHOMPI would confirm
        RunMs(100);
        Tap("KEY_17");
        RunMs(300);
        const int off = Max(SmtLedFull(kFilterKeyLed));
        Press("KEY_5", true);
        RunMs(100);
        Tap("KEY_26");
        RunMs(100);
        Tap("KEY_26");
        RunMs(100);
        Press("KEY_5", false);
        RunMs(2500);
        Check(Max(SmtLedFull(kFilterKeyLed)) > 2 * off, "save-latch: an FX key held, CHOMPI twice while SAVE waits: the effect latches");
        Check(Card("/FRIZZ/frizz_scenes.txt").empty(), "save-latch: and nothing is saved");
    }});

    cases.push_back({"latch-turn", [] {
        RunMs(kReadyMs);
        const int off = Max(SmtLedFull(kTapeStopKeyLed));
        // the tape stop, CHOMPI held, the transport knob turned: still latched
        Press("KEY_10", true);
        RunMs(80);
        Press("KEY_26", true);
        RunMs(80);
        Turn(5, 4);
        RunMs(200);
        Press("KEY_10", false);
        RunMs(100);
        Press("KEY_26", false);
        RunMs(300);
        Check(Max(SmtLedFull(kTapeStopKeyLed)) > 2 * off, "latch-turn: FX key, CHOMPI held, transport turned, key let go: still latched");
        Tap("KEY_10");
        RunMs(300);
        // and a dark knob: the tape stop's 4th
        Press("KEY_10", true);
        RunMs(80);
        Press("KEY_26", true);
        RunMs(80);
        Turn(3, 4);
        RunMs(200);
        Press("KEY_10", false);
        RunMs(100);
        Press("KEY_26", false);
        RunMs(300);
        Check(Max(SmtLedFull(kTapeStopKeyLed)) > 2 * off, "latch-turn: the same with a dark knob: still latched");
    }});

    cases.push_back({"mono", [] {
        RunMs(kReadyMs);
        Tap("ENC_6_SW"); // VOLUME's page 2
        RunMs(300);
        Tap("ENC_6_SW"); // page 3
        RunMs(300);
        for (int d : {-1, -1, 1, -1, -1})
        {
            Turn(kVolumeEncoder, d);
            RunMs(100);
        }
        RunMs(3000);
        const std::string master = Card("/FRIZZ/frizz_master.txt");
        Check(master.find("mono 1") == std::string::npos, "mono: left, left, right, left, left doesn't switch to mono");
        const Rgb stereo = PthLedFull(kVolumeLed);
        for (int i = 0; i < 3; i++)
        {
            Turn(kVolumeEncoder, -1);
            RunMs(100);
        }
        RunMs(3000);
        Check(Card("/FRIZZ/frizz_master.txt").find("mono 1") != std::string::npos, "mono: three lefts in a row do, and it's saved");
        const Rgb mono = PthLedFull(kVolumeLed);
        Check(mono.r == mono.g && mono.g == mono.b && mono.r > 0 && stereo.b > stereo.r, "mono: VOLUME is white for mono, light blue for stereo");
    }});

    cases.push_back({"select-flash", [] {
        RunMs(kReadyMs);
        // a dim compressor key: a select flashes it white
        Press("KEY_26", true);
        RunMs(50);
        Press("KEY_15", true);
        const std::vector<int> dim = Watch(false, kCompKeyLed, 120);
        Press("KEY_15", false);
        Press("KEY_26", false);
        RunMs(300);
        Check(*std::max_element(dim.begin(), dim.end()) > 240, "select-flash: a dim key flashes white");
        // working hard: it flashes dark
        amp = .9f;
        freq = 110.f;
        Tap("KEY_15");
        Turn(1, 40); // ratio up
        RunMs(500);
        Turn(4, 100); // amount all the way
        RunMs(2000);
        Check(Max(SmtLedFull(kCompKeyLed)) > 150, "select-flash: the compressor key lights up as it reduces");
        Press("KEY_26", true);
        RunMs(50);
        Press("KEY_15", true);
        const std::vector<int> bright = Watch(false, kCompKeyLed, 120);
        Press("KEY_15", false);
        Press("KEY_26", false);
        Check(*std::min_element(bright.begin(), bright.end()) == 0, "select-flash: a bright key flashes dark");
    }});

    cases.push_back({"blinks", [] {
        RunMs(kReadyMs);
        // refused (no clock): 3 quick blinks
        Press("KEY_27", true);
        RunMs(80);
        Press("KEY_28", true);
        const std::vector<int> refused = Watch(true, kLoopLed, 800);
        Press("KEY_28", false);
        Press("KEY_27", false);
        const std::vector<int> quick = Runs(refused, 50);
        bool all_quick = !quick.empty();
        for (int r : quick)
            all_quick &= r >= 80 && r <= 120;
        Check(all_quick, "blinks: a refused quantized record blinks quickly (100 ms)");
        // a quantized recording closing: the slow blink
        clock_bpm = 120.f;
        clock_next = NowMs();
        RunMs(1000);
        Press("KEY_27", true);
        RunMs(80);
        Tap("KEY_28");
        Press("KEY_27", false);
        RunMs(2700); // into the 2nd bar
        Tap("KEY_28"); // closes at the bar's end
        const std::vector<int> slow = Runs(Watch(true, kLoopLed, 1100), 50);
        bool all_slow = !slow.empty();
        for (int r : slow)
            all_slow &= r >= 230 && r <= 270;
        Check(all_slow, "blinks: a quantized recording closing blinks slowly (250 ms)");
    }});

    cases.push_back({"transport-leds", [] {
        RunMs(kReadyMs);
        Tap("KEY_28");
        RunMs(2000);
        Tap("KEY_28");
        RunMs(300);
        Turn(5, 40); // up to the fastest forward
        RunMs(800);
        const std::vector<int> fwd = Watch(true, kTransportFwdLed, 1000);
        Check(*std::min_element(fwd.begin(), fwd.end()) > 0, "transport-leds: at full speed forward the LED never goes dark");
        Turn(5, -100); // all the way to the fastest reverse
        RunMs(1500);
        const std::vector<int> rev = Watch(true, kTransportRevLed, 1000);
        Check(*std::min_element(rev.begin(), rev.end()) > 0, "transport-leds: nor at full speed in reverse");
    }});

    cases.push_back({"flanger-click", [] {
        amp = .5f;
        RunMs(kReadyMs);
        Latch("KEY_6");
        Turn(2, 60); // the amount up
        RunMs(800);
        Save(1); // stereo at 0
        // stereo up: the right LFO runs free and drifts; then a recall takes it back to 0 within
        // 25 ms (clicks.cpp's case, on the device). How far they drifted decides the jump, so
        // after several times
        max_step = 0.f; // over the presses too: the jump comes as the recall lands
        for (uint32_t drift : {6000, 1500, 2500, 3500, 4500})
        {
            Turn(3, 50);
            RunMs(drift);
            Tap("KEY_17");
            RunMs(600);
        }
        const float most = max_step;
        const float mono = StepMs(1000); // the flanger as saved, stereo at 0
        printf("      flanger: largest step %.4f around the recalls, %.4f as saved\n", most, mono);
        Check(most < mono * 1.5f, "flanger-click: stereo turned up, then a recall back to 0: no click in either channel");
    }});

    cases.push_back({"freezer-click", [] {
        RunMs(kReadyMs);
        amp = .5f;
        freq = 520.f;
        const float live = StepMs(300); // the input the new press captures, as it is
        freq = 220.f;
        RunMs(100);
        Press("KEY_1", true);
        RunMs(2500);
        freq = 520.f; // the live input now differs from what's frozen
        const float frozen = std::max(live, StepMs(500));
        Press("KEY_1", false);
        RunMs(8); // just into the repeats' fade (a key needs 7 ms to count as let go)
        max_step = 0.f; // from the press on
        Press("KEY_1", true);
        RunMs(300);
        const float again = max_step;
        Press("KEY_1", false);
        printf("      freezer: largest step %.4f pressed again, %.4f frozen or live\n", again, frozen);
        Check(again < frozen * 1.5f, "freezer-click: pressed again while the repeats fade: no click");
    }});

    // a late card: three power cycles on one card
    cases.push_back({"card-1", [] {
        RunMs(kReadyMs);
        Latch("KEY_6");
        Save(2); // the card's own scene, in slot 2
        Tap("KEY_15");
        Turn(4, 20); // the card's own compressor
        RunMs(3000);
        Check(SavedLatch(Card("/FRIZZ/frizz_scenes.txt"), 2, "flanger") == 1, "card: a scene saved into slot 2 is on the card");
        KeepCard();
    }});

    cases.push_back({"card-2", [] {
        TakeCard();
        const std::string old_scenes = Card("/FRIZZ/frizz_scenes.txt");
        const std::string old_master = Card("/FRIZZ/frizz_master.txt");
        SetCardPresent(false);
        RunMs(kReadyMs);
        Latch("KEY_5");
        Tap("KEY_25");
        RunMs(100);
        Tap("KEY_17");
        RunMs(300);
        Tap("KEY_26");
        int red = 0;
        for (int i = 0; i < 300; i++)
        {
            RunMs(1);
            const Rgb c = SmtLedFull(kSlot1Led);
            red += c.r > 150 && c.g < 60 && c.b < 60;
        }
        Check(red > 0, "card: without a card, a save flashes the slot red");
        Tap("KEY_15");
        Turn(4, 40); // a compressor of this session
        RunMs(3000);
        SetCardPresent(true);
        RunMs(500);
        Save(1);
        RunMs(2500);
        const std::string scenes = Card("/FRIZZ/frizz_scenes.txt");
        Check(SavedLatch(scenes, 1, "filter") == 1 && SavedLatch(scenes, 2, "flanger") == 1, "card: put in, the next save keeps the card's scene and adds this one");
        Check(Card("/FRIZZ/frizz_master.bak") == old_master && !old_master.empty(), "card: frizz_master.bak holds the card's old compressor");
        Check(Card("/FRIZZ/frizz_master.txt") != old_master && !Card("/FRIZZ/frizz_master.txt").empty(), "card: frizz_master.txt the one played with");
        Check(Card("/FRIZZ/frizz_scenes.bak").empty(), "card: no scene of the card overwritten, so no frizz_scenes.bak");
        KeepCard();
        (void)old_scenes;
    }});

    cases.push_back({"card-3", [] {
        TakeCard();
        RunMs(kReadyMs);
        Check(Max(SmtLedFull(kSlot1Led)) > 0 && Max(SmtLedFull(kSlot1Led + 1)) > 0 && Max(SmtLedFull(kSlot1Led + 2)) == 0,
              "card: after a reboot both scenes are there, the empty slot dark");
        // a session without the card saves into slot 2, which the card also has
        KeepCard();
    }});

    cases.push_back({"card-4", [] {
        TakeCard();
        const std::string old_scenes = Card("/FRIZZ/frizz_scenes.txt");
        SetCardPresent(false);
        RunMs(kReadyMs);
        Latch("KEY_2");
        Save(2);
        SetCardPresent(true);
        RunMs(500);
        Save(2);
        RunMs(2500);
        Check(Card("/FRIZZ/frizz_scenes.bak") == old_scenes, "card: saving into a slot the card also has keeps the card's scenes as frizz_scenes.bak");
        Check(SavedLatch(Card("/FRIZZ/frizz_scenes.txt"), 2, "shifter") == 1, "card: and frizz_scenes.txt has the new one");
    }});

    // the event log (EventLog.h): a session, SHIFT + transport press, its file on the card
    cases.push_back({"bug-log", [] {
        TakeCard(); // card-3's: scenes in slots 1 and 2
        CardFiles().erase("/FRIZZ/frizz_scenes.bak");
        CardFiles().erase("/FRIZZ/frizz_master.bak");
        std::ofstream leds(card_file + ".leds");
        auto run = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t++)
            {
                RunMs(1);
                leds << NowMs() << ' ' << LedLine() << '\n';
            }
        };
        run(kReadyMs);
        Latch("KEY_6");
        Turn(4, 7);              // knob 1
        Press("KEY_26", true);   // SHIFT + knob 1: coarse
        run(20);
        Turn(4, -3);
        run(60);
        Press("KEY_26", false);
        run(200);
        Tap("KEY_28");           // record
        run(2000);
        Tap("KEY_28");           // play
        run(500);
        Turn(5, 6);              // the transport: speed
        run(500);
        Tap("KEY_18");           // the card's scene in slot 2
        run(400);
        Save(3);                 // a change to the card after power-on
        run(300);
        Tap("KEY_6");
        run(1000);
        Press("KEY_26", true);
        run(100);
        const uint32_t combo = NowMs();
        Press("ENC_5_SW", true);
        leds << "# combo " << combo << '\n';
        bool white = false;
        for (int i = 0; i < 400; i++)
        {
            RunMs(1);
            const Rgb rev = PthLedFull(kTransportRevLed), fwd = PthLedFull(kTransportFwdLed);
            white |= std::min({rev.r, rev.g, rev.b, fwd.r, fwd.g, fwd.b}) > 200;
        }
        Press("ENC_5_SW", false);
        Press("KEY_26", false);
        RunMs(1000);
        const std::string log = Card("/FRIZZ/bug-1.txt");
        // the card goes on as before: a save and the compressor land in /FRIZZ
        const std::string scenes = Card("/FRIZZ/frizz_scenes.txt");
        const std::string master = Card("/FRIZZ/frizz_master.txt");
        Save(4);
        Tap("KEY_15");
        Turn(4, 10);
        RunMs(3000);
        Check(Card("/FRIZZ/frizz_scenes.txt") != scenes && Card("/FRIZZ/frizz_master.txt") != master
                  && Card("/frizz_scenes.txt").empty() && Card("/frizz_master.txt").empty(),
              "bug log: saves after it still go to /FRIZZ");
        Check(log.rfind("# FRIZZ event log 1:", 0) == 0 && log.find("# written here") != std::string::npos,
              "bug log: SHIFT + transport press writes /FRIZZ/bug-1.txt");
        Check(white, "bug log: and the transport LEDs blink white");
        Check(log.find("card file /FRIZZ/frizz_scenes.txt\n|") != std::string::npos
                  && log.find("|scene 3") == std::string::npos,
              "bug log: with the card's scenes as they were at power-on, not as saved since");
        Check(log.find("\nturn 5 1\n") != std::string::npos && log.find("\nup KEY_28\n") != std::string::npos,
              "bug log: and the session's keys and knobs");
        KeepCard();
    }});

    // the log played back on a fresh twin: the same LEDs every ms up to the combo, and the
    // same log written at the end
    cases.push_back({"bug-replay", [] {
        TakeCard();
        const std::string log = Card("/FRIZZ/bug-1.txt");
        CardFiles().clear(); // the card is what the log says
        FILE* leds = tmpfile();
        std::istringstream script(log + "\nwait 1000\n");
        const int failed = PlayScript(script, leds, nullptr);
        Check(failed == 0 && !log.empty(), "bug log: the twin plays it without a fault");

        // the original, ms by ms, and the replay's changes
        std::ifstream orig(card_file + ".leds");
        std::map<uint32_t, std::string> want;
        uint32_t combo = 0;
        std::string line;
        while (std::getline(orig, line))
        {
            if (line.rfind("# combo ", 0) == 0)
                combo = std::stoul(line.substr(8));
            else
                want[std::stoul(line)] = line.substr(line.find(' ') + 1);
        }
        rewind(leds);
        std::map<uint32_t, std::string> got_changes;
        char buf[1024];
        while (fgets(buf, sizeof(buf), leds))
        {
            std::string l(buf);
            l.erase(l.find_last_not_of(" \n") + 1);
            const size_t sp = l.find_first_not_of(' ');
            const size_t end = l.find(' ', sp);
            got_changes[std::stoul(l.substr(sp, end - sp))] = l.substr(l.find("pth"));
        }
        fclose(leds);
        uint32_t differ = 0, first = 0;
        std::string got;
        auto next = got_changes.begin();
        for (auto& w : want)
        {
            if (w.first >= combo)
                break;
            while (next != got_changes.end() && next->first <= w.first)
                got = (next++)->second;
            if (got != w.second && !differ++)
                first = w.first;
        }
        if (differ)
            printf("      %u ms differ, from %u ms\n", differ, first);
        Check(combo > 0 && differ == 0, "bug log: played back, the LEDs are the session's, every ms up to the combo");

        // and it ends at the same combo, so the replay writes the same events
        const std::string again = Card("/FRIZZ/bug-1.txt");
        auto events = [](const std::string& t) { return t.substr(t.find("booted\n")); };
        Check(again.find("booted\n") != std::string::npos && events(again) == events(log),
              "bug log: and the replay writes the same log again");
        unlink((card_file + ".leds").c_str());
    }});

    char card_name[] = "/tmp/frizz-ui-card-XXXXXX";
    const int card_fd = mkstemp(card_name);
    close(card_fd);
    card_file = card_name;

    int failed = 0;
    for (auto& c : cases)
    {
        fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            // a replay boots by its script
            if (strcmp(c.first, "bug-replay") != 0)
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
    unlink(card_name);
    failures = failed;
    return Finish();
}
