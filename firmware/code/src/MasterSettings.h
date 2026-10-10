/** @file MasterSettings.h
 *  @brief What the play page keeps on the card outside the scenes: the master compressor's
 *  knobs (MasterComp.h), the settings page's (SettingsPage.h: the mono input, the MIDI
 *  settings, the clock's tempo factor and source, MIDI out, the LEDs' brightness), and their text
 *  format (SceneStore.h writes it). No
 *  hardware here, so the format can be tested on the host.
 *
 *  The file, one line per setting, keyed by its name, the values in millionths as in the
 *  scene file (FxScenes.h):
 *
 *    FRIZZ master 1
 *    compressor2 300000 500000 500000 500000 1000000 0 0 0
 *    mono 0
 *    midi_channel 16
 *    midi_transport 0
 *    clock_factor 100
 *    clock_source 0
 *    midi_out 0
 *    led_brightness 100
 *
 *  clock_factor is how FRIZZ follows a MIDI clock, in percent of its tempo: 50, 100 or 200;
 *  clock_source whose clock it follows (ClockSource, MidiClock.h): 0 Auto, 1 TRS, 2 USB, 3
 *  internal; midi_out where MIDI out goes (MidiOutPorts): 0 off, 1 the jack, 2 the jack and
 *  USB; led_brightness the LEDs' in percent of FRIZZ's full: 100, 75 or 50. Another
 *  value keeps the default. compressor2 has the compressor's eight parameters, both pages
 *  (MasterComp.h); a file from before has "compressor" with four, threshold, ratio, speed and
 *  mix, which loads with the speed as both attack and release and the makeup at 0dB (it was
 *  automatic then).
 *  Reading, unknown lines are skipped and a setting the file leaves out keeps its default, so
 *  more settings can join later. A file from before the randomizer was removed still has its
 *  line; it's skipped, and the next write leaves it out.
 */
#pragma once
#include "FxParams.h"
#include "FxScenes.h"

namespace chompi
{

// The file's size at most
static const size_t kMasterFileMax = 512;

struct MasterSettings
{
    float comp[kNumFxParams];
    bool mono;  // the AUX input's left channel to both sides, for a mono (TS) cable
    uint8_t midi_channel; // the channel FRIZZ listens on, 1-16, or 0 for all
    bool midi_transport;  // MIDI Start, Continue and Stop play and pause the loop
    uint8_t clock_factor;   // percent of the clock's tempo FRIZZ follows: 50, 100, 200
    uint8_t clock_source;   // whose clock: 0 Auto, 1 TRS, 2 USB, 3 internal (MidiClock.h's ClockSource)
    uint8_t midi_out;       // where MIDI out goes: 0 off, 1 the jack, 2 and USB (MidiOutPorts)
    uint8_t led_brightness; // percent: 100, 75, 50

    /** Every setting on its default */
    void Reset()
    {
        for (size_t p = 0; p < kNumFxParams; p++)
            comp[p] = kCompParams.defaults[p];
        mono = false;
        midi_channel = kDefaultMidiChannel;
        midi_transport = false;
        clock_factor = 100;
        clock_source = 0;
        midi_out = 0;
        led_brightness = 100;
    }

    // the last channel, which a setup sending notes to other instruments uses least
    static const uint8_t kDefaultMidiChannel = 16;
};

namespace masterfile
{
static const char kHeader[] = "FRIZZ master 1";
} // namespace masterfile

/** The settings as the file's text, into buf (terminated). Returns its length, or 0 if it
 *  didn't fit */
inline size_t FormatMaster(const MasterSettings& settings, char* buf, size_t size)
{
    using namespace scenefile;
    size_t pos = 0;
    Put(buf, size, pos, masterfile::kHeader);
    Put(buf, size, pos, "\ncompressor2");
    PutValues(buf, size, pos, settings.comp, kNumFxParams);
    Put(buf, size, pos, settings.mono ? "\nmono 1" : "\nmono 0");
    Put(buf, size, pos, "\nmidi_channel ");
    PutUint(buf, size, pos, settings.midi_channel);
    Put(buf, size, pos, settings.midi_transport ? "\nmidi_transport 1" : "\nmidi_transport 0");
    Put(buf, size, pos, "\nclock_factor ");
    PutUint(buf, size, pos, settings.clock_factor);
    Put(buf, size, pos, "\nclock_source ");
    PutUint(buf, size, pos, settings.clock_source);
    Put(buf, size, pos, "\nmidi_out ");
    PutUint(buf, size, pos, settings.midi_out);
    Put(buf, size, pos, "\nled_brightness ");
    PutUint(buf, size, pos, settings.led_brightness);
    Put(buf, size, pos, "\n");
    buf[pos] = '\0';
    return pos + 1 < size ? pos : 0;
}

/** Reads the file's text into settings, which start on their defaults. Returns false, with
 *  every setting on its default, if it isn't a settings file */
inline bool ParseMaster(const char* text, MasterSettings& settings)
{
    using namespace scenefile;
    settings.Reset();

    const char* p = AfterHeader(text, masterfile::kHeader);
    if (!p)
        return false;
    bool new_comp = false; // compressor2 read: an old line after it is ignored

    while (*p)
    {
        NextLine(p);

        size_t len;
        const char* word = Word(p, len);
        if (word && Is(word, len, "mono"))
        {
            const char* num = Word(p, len);
            if (num)
                settings.mono = strtol(num, nullptr, 10) != 0;
            continue;
        }
        if (word && Is(word, len, "midi_channel"))
        {
            const char* num = Word(p, len);
            const long v = num ? strtol(num, nullptr, 10) : -1;
            if (v >= 0 && v <= 16)
                settings.midi_channel = static_cast<uint8_t>(v);
            continue;
        }
        if (word && Is(word, len, "midi_transport"))
        {
            const char* num = Word(p, len);
            if (num)
                settings.midi_transport = strtol(num, nullptr, 10) != 0;
            continue;
        }
        if (word && Is(word, len, "clock_factor"))
        {
            const char* num = Word(p, len);
            const long v = num ? strtol(num, nullptr, 10) : -1;
            if (v == 50 || v == 100 || v == 200)
                settings.clock_factor = static_cast<uint8_t>(v);
            continue;
        }
        if (word && Is(word, len, "clock_source"))
        {
            const char* num = Word(p, len);
            const long v = num ? strtol(num, nullptr, 10) : -1;
            if (v >= 0 && v <= 3)
                settings.clock_source = static_cast<uint8_t>(v);
            continue;
        }
        if (word && Is(word, len, "midi_out"))
        {
            const char* num = Word(p, len);
            const long v = num ? strtol(num, nullptr, 10) : -1;
            if (v >= 0 && v <= 2)
                settings.midi_out = static_cast<uint8_t>(v);
            continue;
        }
        if (word && Is(word, len, "led_brightness"))
        {
            const char* num = Word(p, len);
            const long v = num ? strtol(num, nullptr, 10) : -1;
            if (v == 50 || v == 75 || v == 100)
                settings.led_brightness = static_cast<uint8_t>(v);
            continue;
        }
        if (word && Is(word, len, "compressor2"))
        {
            ReadValues(p, settings.comp, kNumFxParams);
            new_comp = true;
            continue;
        }
        if (word && Is(word, len, "compressor") && !new_comp)
        {
            // before page 2: threshold, ratio, speed (attack and release together), mix
            float old[kNumFxKnobs] = {settings.comp[0], settings.comp[1], settings.comp[2],
                                      settings.comp[4]};
            ReadValues(p, old, kNumFxKnobs);
            settings.comp[0] = old[0];
            settings.comp[1] = old[1];
            settings.comp[2] = settings.comp[3] = old[2];
            settings.comp[4] = old[3];
        }
    }
    return true;
}

} // namespace chompi
