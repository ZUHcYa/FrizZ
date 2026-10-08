/** @file cli.cpp
 *  @brief frizz-twin: plays a script of key presses, knob turns, MIDI and audio into the
 *  virtual CHOMPI (twin.h) from power-on, and writes what came out: the master out as a WAV,
 *  the LEDs whenever they change. See README.md for the script's commands.
 */
#include "twin.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace twin;

static std::vector<float> out_samples; // the master out, interleaved L R
static FILE* led_log = nullptr;
static std::string last_leds;
static bool quiet = false;
static int failures = 0;

// the audio into AUX (L and R): a sine, a WAV file looped, or nothing
static enum { IN_OFF, IN_SINE, IN_WAV } input = IN_OFF;
static float sine_freq = 220.f, sine_amp = .3f, sine_phase = 0.f;
static std::vector<float> wav_in; // interleaved L R
static size_t wav_pos = 0;

// MIDI clock out of a virtual sequencer: 24 ticks a beat
static float clock_bpm = 0.f;
static double clock_next_ms = 0.;

static std::string Hex(const Rgb& c)
{
    char s[8];
    snprintf(s, sizeof(s), "%02x%02x%02x", c.r, c.g, c.b);
    return s;
}

static std::string LedLine()
{
    std::string s = "pth";
    for (int i = 0; i < kNumPthLeds; i++)
        s += " " + Hex(PthLedFull(i));
    s += " | smt";
    for (int i = 0; i < kNumSmtLeds; i++)
        s += " " + Hex(SmtLedFull(i));
    return s;
}

static void LogLeds()
{
    const std::string now = LedLine();
    if (now == last_leds)
        return;
    last_leds = now;
    if (led_log)
        fprintf(led_log, "%7u %s\n", NowMs(), now.c_str());
}

static void RunMs(uint32_t ms)
{
    const size_t blocks = ms * 2;
    float in[kBlockSize * kChannels], out[kBlockSize * kChannels];
    for (size_t b = 0; b < blocks; b++)
    {
        while (clock_bpm > 0.f && clock_next_ms <= NowMs() + .5)
        {
            Midi(0xF8);
            clock_next_ms += 60000. / (clock_bpm * 24.);
        }
        for (size_t i = 0; i < kBlockSize; i++)
        {
            float l = 0.f, r = 0.f;
            if (input == IN_SINE)
            {
                l = r = sine_amp * sinf(sine_phase);
                sine_phase += 2.f * float(M_PI) * sine_freq / kSampleRate;
                if (sine_phase > 2.f * float(M_PI))
                    sine_phase -= 2.f * float(M_PI);
            }
            else if (input == IN_WAV && !wav_in.empty())
            {
                l = wav_in[wav_pos];
                r = wav_in[wav_pos + 1];
                wav_pos = (wav_pos + 2) % wav_in.size();
            }
            in[i * kChannels + 0] = 0.f;
            in[i * kChannels + 1] = 0.f;
            in[i * kChannels + 2] = l;
            in[i * kChannels + 3] = r;
        }
        Run(1, in, out);
        for (size_t i = 0; i < kBlockSize; i++)
        {
            out_samples.push_back(out[i * kChannels + 2]);
            out_samples.push_back(out[i * kChannels + 3]);
        }
        if (b % 2 == 1)
            LogLeds();
    }
}

// A WAV file's samples as interleaved stereo floats: 16-bit PCM or 32-bit float, mono or stereo
static bool ReadWav(const char* path, std::vector<float>& to)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4))
        return false;
    uint16_t format = 0, channels = 0, bits = 0;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        uint32_t len;
        memcpy(&len, &d[p + 4], 4);
        if (!memcmp(&d[p], "fmt ", 4))
        {
            memcpy(&format, &d[p + 8], 2);
            memcpy(&channels, &d[p + 10], 2);
            memcpy(&bits, &d[p + 22], 2);
        }
        else if (!memcmp(&d[p], "data", 4) && channels)
        {
            const size_t frames = len / (channels * bits / 8);
            for (size_t i = 0; i < frames; i++)
                for (int c = 0; c < 2; c++)
                {
                    const size_t at = p + 8 + (i * channels + (c < channels ? c : 0)) * bits / 8;
                    float v = 0.f;
                    if (format == 1 && bits == 16)
                    {
                        int16_t s;
                        memcpy(&s, &d[at], 2);
                        v = s / 32768.f;
                    }
                    else if (format == 3 && bits == 32)
                        memcpy(&v, &d[at], 4);
                    else
                        return false;
                    to.push_back(v);
                }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

static void WriteWav(const char* path, const std::vector<float>& s)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    const uint32_t data = s.size() * 4, riff = 36 + data, fmt_len = 16, rate = 48000,
                   bytes_per_sec = rate * 8;
    const uint16_t format = 3, channels = 2, align = 8, bits = 32;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&bytes_per_sec, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fwrite(s.data(), 4, s.size(), f);
    fclose(f);
}

static void Fail(int line, const std::string& what)
{
    fprintf(stderr, "line %d: %s\n", line, what.c_str());
    failures++;
}

int main(int argc, char** argv)
{
    const char* script = nullptr;
    const char* wav_out = nullptr;
    const char* led_out = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            wav_out = argv[++i];
        else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            led_out = argv[++i];
        else if (!strcmp(argv[i], "-q"))
            quiet = true;
        else
            script = argv[i];
    }
    if (!script)
    {
        fprintf(stderr, "usage: frizz-twin [-o out.wav] [-l leds.txt] [-q] SCRIPT|-\n");
        return 2;
    }
    if (led_out)
        led_log = !strcmp(led_out, "-") ? stdout : fopen(led_out, "w");

    std::ifstream file;
    std::istream* src = &std::cin;
    if (strcmp(script, "-"))
    {
        file.open(script);
        if (!file)
        {
            fprintf(stderr, "can't read %s\n", script);
            return 2;
        }
        src = &file;
    }

    std::string text;
    int line_no = 0;
    bool booted = false;
    auto boot = [&]() {
        if (!booted)
            Boot();
        booted = true;
    };
    while (std::getline(*src, text))
    {
        line_no++;
        std::istringstream in(text);
        std::string cmd;
        if (!(in >> cmd) || cmd[0] == '#')
            continue;

        if (cmd == "card")
        {
            // card put PATH FILE / card text PATH "..." / card dump
            std::string what, path;
            in >> what;
            if (what == "put" && in >> path)
            {
                std::string from;
                in >> from;
                std::ifstream f(from, std::ios::binary);
                if (!f)
                    Fail(line_no, "can't read " + from);
                std::stringstream ss;
                ss << f.rdbuf();
                CardFiles()[path] = ss.str();
            }
            else if (what == "remove")
                SetCardPresent(false);
            else if (what == "insert")
                SetCardPresent(true);
            else if (what == "dump")
                for (auto& f : CardFiles())
                    printf("--- %s\n%s\n", f.first.c_str(), f.second.c_str());
            else
                Fail(line_no, "card: put PATH FILE, remove, insert or dump");
            continue;
        }
        if (cmd == "battery")
        {
            float v = 3.8f;
            std::string a, b;
            in >> v >> a >> b;
            SetBattery(v, a == "plugged" || b == "plugged", a == "full" || b == "full");
            continue;
        }
        if (cmd == "input")
        {
            std::string what;
            in >> what;
            if (what == "sine")
            {
                in >> sine_freq >> sine_amp;
                input = IN_SINE;
            }
            else if (what == "wav")
            {
                std::string path;
                in >> path;
                wav_in.clear();
                wav_pos = 0;
                if (ReadWav(path.c_str(), wav_in))
                    input = IN_WAV;
                else
                    Fail(line_no, "can't read " + path + " (16-bit PCM or 32-bit float WAV)");
            }
            else
                input = IN_OFF;
            continue;
        }

        // everything else happens to the running device
        boot();
        if (cmd == "wait")
        {
            uint32_t ms = 0;
            in >> ms;
            RunMs(ms);
        }
        else if (cmd == "at")
        {
            uint32_t ms = 0;
            in >> ms;
            if (ms > NowMs())
                RunMs(ms - NowMs());
        }
        else if (cmd == "down" || cmd == "up" || cmd == "tap")
        {
            std::string name;
            uint32_t hold = 60;
            in >> name >> hold;
            if (!Press(name.c_str(), cmd != "up"))
                Fail(line_no, "no key " + name);
            if (cmd == "tap")
            {
                RunMs(hold);
                Press(name.c_str(), false);
            }
        }
        else if (cmd == "turn")
        {
            int enc = 0, detents = 0;
            in >> enc >> detents;
            Turn(enc, detents);
        }
        else if (cmd == "toggle")
        {
            int level = 1;
            in >> level;
            SetToggle(level);
        }
        else if (cmd == "midi")
        {
            std::string b;
            while (in >> b)
                Midi(static_cast<uint8_t>(strtol(b.c_str(), nullptr, 16)));
        }
        else if (cmd == "clock")
        {
            in >> clock_bpm;
            clock_next_ms = NowMs();
        }
        else if (cmd == "leds")
            printf("%7u %s\n", NowMs(), LedLine().c_str());
        else if (cmd == "expect")
        {
            // expect led pth|smt INDEX RRGGBB (at full scale), expect off, expect on
            std::string what, chain, want;
            int index = 0;
            in >> what;
            if (what == "led" && in >> chain >> index >> want)
            {
                const std::string got = Hex(chain == "pth" ? PthLedFull(index)
                                                           : SmtLedFull(index));
                if (got != want)
                    Fail(line_no, chain + " " + std::to_string(index) + " is " + got + ", not " +
                                      want);
            }
            else if (what == "off" && Powered())
                Fail(line_no, "still powered");
            else if (what == "on" && !Powered())
                Fail(line_no, "powered off");
        }
        else
            Fail(line_no, "unknown command " + cmd);
    }

    if (wav_out)
        WriteWav(wav_out, out_samples);
    if (led_log && led_log != stdout)
        fclose(led_log);
    if (!quiet)
    {
        float peak = 0.f;
        double sum = 0.;
        for (float v : out_samples)
        {
            peak = std::max(peak, std::fabs(v));
            sum += double(v) * v;
        }
        fprintf(stderr, "%u ms, master out peak %.3f rms %.4f%s\n", NowMs(), peak,
                out_samples.empty() ? 0. : sqrt(sum / out_samples.size()),
                Powered() ? "" : ", powered off");
    }
    return failures ? 1 : 0;
}
