// bench.cpp: runs the CPU bench's firmware (FRIZZ-bench.bin, Bench.h) on the virtual CHOMPI
// from power-on to its end and checks that it gets there: every segment run and written to
// /FRIZZ/cpu.txt, the keys' LEDs graded, the panel green, the effects actually switched in
// and out. The loads themselves are 0 here: no time passes on the twin while the callback
// runs, so only the device measures them. Run by unit.sh bench.
// twin defines: -DFRIZZ_BENCH=1
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "check.h"
#include "twin.h"

using namespace twin;

static const char* const kSegments[] = {
    "idle",      "freezer",  "shifter",    "folder",     "crusher",         "filter",
    "flanger",   "resonator", "slicer",    "warble",     "tapestop",        "delay",
    "reverb",    "compressor", "inserts",  "everything", "stress",          "recording",
    "loop",      "loop+delay", "loop+scene4+dly", "loop+everything",
};
static const size_t kNum = sizeof(kSegments) / sizeof(kSegments[0]);

int main()
{
    Boot();
    // the boot animation, then 22 segments of 3 s and the 4 s recording among them; the input
    // is the bench's own, so none is fed here
    std::vector<float> rms; // the master out's level every 3 s from when the bench starts
    float out[kBlockSize * kChannels];
    double sum = 0.;
    const uint32_t start_ms = 1400, end_ms = start_ms + 3000 * kNum + 1000 + 2000;
    for (uint32_t b = 0; b < end_ms * 2; b++)
    {
        Run(1, nullptr, out);
        for (size_t i = 0; i < kBlockSize; i++)
            sum += out[i * kChannels + 2] * out[i * kChannels + 2];
        if (NowMs() >= start_ms && (NowMs() - start_ms) % 3000 == 0 && b % 2 == 1)
        {
            rms.push_back(sqrtf(sum / (6000 * kBlockSize)));
            sum = 0.;
        }
    }

    const auto& card = CardFiles();
    const auto it = card.find("/FRIZZ/cpu.txt");
    Check(it != card.end(), "bench: /FRIZZ/cpu.txt is written at the end");
    const std::string text = it == card.end() ? "" : it->second;
    size_t found = 0;
    std::istringstream in(text);
    std::string line;
    for (size_t s = 0; std::getline(in, line);)
        if (s < kNum && line.rfind(kSegments[s], 0) == 0 && line.size() >= 30)
        {
            found++;
            s++;
        }
    Check(found == kNum, "bench: every segment has its line, in order, with a max and an average");
    Check(text.find("# worst: ") != std::string::npos, "bench: and the worst is named");

    bool keys = true;
    for (int led : {24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 0, 1, 2, 3, 4, 5, 6})
    {
        const Rgb c = SmtLedFull(led);
        keys &= c.g > 200 && c.r == 0 && c.b == 0; // green: the twin's loads are 0
    }
    Check(keys, "bench: a key per segment, graded green");
    bool panel = true;
    for (int i = 0; i < kNumPthLeds; i++)
        panel &= PthLedFull(i).g > 200 && PthLedFull(i).r == 0;
    Check(panel, "bench: at the end the panel is green");

    // the signal goes through: every 3 s after the boot something comes out, but for the tape
    // stop's segment (the windows don't line up with the segments exactly)
    int quiet = 0;
    for (size_t w = 1; w < rms.size(); w++)
        quiet += rms[w] < .002f;
    Check(rms.size() >= kNum && quiet <= 1, "bench: its signal reaches the master out throughout");
    return Finish();
}
