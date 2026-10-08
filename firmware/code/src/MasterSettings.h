/** @file MasterSettings.h
 *  @brief What the play page keeps on the card outside the scenes: the master compressor's
 *  knobs (MasterComp.h) and the mono input, and their text format (SceneStore.h writes it). No
 *  hardware here, so the format can be tested on the host.
 *
 *  The file, one line per setting, keyed by its name, the values in millionths as in the
 *  scene file (FxScenes.h):
 *
 *    FRIZZ master 1
 *    compressor 300000 500000 500000 1000000
 *    mono 0
 *
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

    /** Every setting on its default */
    void Reset()
    {
        for (size_t p = 0; p < kNumFxParams; p++)
            comp[p] = kCompParams.defaults[p];
        mono = false;
    }
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
    Put(buf, size, pos, "\ncompressor");
    for (size_t p = 0; p < kNumFxParams; p++)
    {
        const float val = fclamp(settings.comp[p], 0.f, 1.f);
        Put(buf, size, pos, " ");
        PutUint(buf, size, pos, static_cast<uint32_t>(val * kScale + .5f));
    }
    Put(buf, size, pos, settings.mono ? "\nmono 1" : "\nmono 0");
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

    const size_t header_len = sizeof(masterfile::kHeader) - 1;
    if (strncmp(text, masterfile::kHeader, header_len) != 0)
        return false;
    const char after = text[header_len];
    if (after != '\0' && after != '\n' && after != '\r' && after != ' ' && after != '\t')
        return false;

    const char* p = text + header_len;
    while (*p)
    {
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;

        size_t len;
        const char* word = Word(p, len);
        if (word && Is(word, len, "mono"))
        {
            const char* num = Word(p, len);
            if (num)
                settings.mono = strtol(num, nullptr, 10) != 0;
            continue;
        }
        if (!word || !Is(word, len, "compressor"))
            continue;
        for (size_t i = 0; i < kNumFxParams; i++)
        {
            const char* num = Word(p, len);
            if (!num)
                break;
            const float val = static_cast<float>(strtol(num, nullptr, 10)) / kScale;
            settings.comp[i] = fclamp(val, 0.f, 1.f);
        }
    }
    return true;
}

} // namespace chompi
