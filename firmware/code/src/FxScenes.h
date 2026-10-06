/** @file FxScenes.h
 *  @brief FX scenes: the punch-in FX's parameters and latches, saved to a slot and recalled
 *  from it (NormalPage.h), and their text format on the SD card (SceneStore.h). No hardware
 *  here, so the format can be tested on the host.
 *
 *  The file, one line per effect, keyed by its name (kFxNames) so scenes outlive new effects
 *  and a new order. The number after the name is the latch, then the parameters in millionths:
 *
 *    FRIZZ scenes 1
 *    scene 2
 *    freezer 1 285714 0 0 0
 *    filter 0 300000 500000 0 666700
 *
 *  Reading, unknown names and lines are skipped and an effect a scene leaves out gets its
 *  defaults. Millionths because a coarse grid's points must come back on the grid: the
 *  resonator's pitch grid is .0157 apart and counts a value within 1.6e-4 as on a point.
 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "FxChain.h"

namespace chompi
{

// The scenes saved on the card, "scene 1" to "scene 4" in the file
static const size_t kNumScenes = 4;
// The play page's slots: the blank scene first, a fixed one that unlatches every effect and
// puts every knob on its default (SceneControls.h), never stored; then the saved ones, slot s
// holding the file's scene s
static const size_t kBlankSlot = 0;
static const size_t kNumSlots = kNumScenes + 1;
// The file's size at most: 4 scenes of 12 effects take under 2.5KB
static const size_t kSceneFileMax = 4096;

struct FxScene
{
    bool used;
    uint16_t latched; // bit fx: latched
    float params[kNumFx][kNumFxParams];
};
static_assert(kNumFx <= 16, "a latch bit per effect");

namespace scenefile
{
static const char kHeader[] = "FRIZZ scenes 1";
static const float kScale = 1000000.f;

/** Appends s to buf at pos, keeping room for the terminator */
inline void Put(char* buf, size_t size, size_t& pos, const char* s)
{
    while (*s && pos + 1 < size)
        buf[pos++] = *s++;
}

inline void PutUint(char* buf, size_t size, size_t& pos, uint32_t n)
{
    char digits[11];
    size_t len = 0;
    do
    {
        digits[len++] = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n);
    while (len && pos + 1 < size)
        buf[pos++] = digits[--len];
}

/** The next whitespace-separated word from p: its start, and p moved past it; nullptr at the
 *  end of the line */
inline const char* Word(const char*& p, size_t& len)
{
    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    if (*p == '\0' || *p == '\n')
        return nullptr;
    const char* start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        p++;
    len = static_cast<size_t>(p - start);
    return start;
}

inline bool Is(const char* word, size_t len, const char* s)
{
    return strlen(s) == len && strncmp(word, s, len) == 0;
}
} // namespace scenefile

/** The used scenes as the file's text, into buf (terminated). Returns its length, or 0 if it
 *  didn't fit */
inline size_t FormatScenes(const FxScene* scenes, char* buf, size_t size)
{
    using namespace scenefile;
    size_t pos = 0;
    Put(buf, size, pos, kHeader);
    Put(buf, size, pos, "\n");
    for (size_t s = 0; s < kNumScenes; s++)
    {
        if (!scenes[s].used)
            continue;
        Put(buf, size, pos, "scene ");
        PutUint(buf, size, pos, s + 1);
        Put(buf, size, pos, "\n");
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            Put(buf, size, pos, kFxNames[fx]);
            Put(buf, size, pos, scenes[s].latched & (1u << fx) ? " 1" : " 0");
            for (size_t p = 0; p < kNumFxParams; p++)
            {
                const float val = fclamp(scenes[s].params[fx][p], 0.f, 1.f);
                Put(buf, size, pos, " ");
                PutUint(buf, size, pos, static_cast<uint32_t>(val * kScale + .5f));
            }
            Put(buf, size, pos, "\n");
        }
    }
    buf[pos] = '\0';
    // full means cut short
    return pos + 1 < size ? pos : 0;
}

/** Reads the file's text into scenes, all of them: a scene the text lacks is unused, an
 *  effect it lacks gets defaults. Returns false, with every scene unused, if it isn't a
 *  scene file */
inline bool ParseScenes(const char* text,
                        const float (*defaults)[kNumFxParams],
                        FxScene* scenes)
{
    using namespace scenefile;
    for (size_t s = 0; s < kNumScenes; s++)
        scenes[s].used = false;

    const size_t header_len = sizeof(kHeader) - 1;
    if (strncmp(text, kHeader, header_len) != 0)
        return false;
    // the whole word: "FRIZZ scenes 10" is another version
    const char after = text[header_len];
    if (after != '\0' && after != '\n' && after != '\r' && after != ' ' && after != '\t')
        return false;

    FxScene* scene = nullptr;
    const char* p = text + header_len;
    while (*p)
    {
        // the start of the next line
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;

        size_t len;
        const char* word = Word(p, len);
        if (!word)
            continue;

        if (Is(word, len, "scene"))
        {
            scene = nullptr;
            const char* num = Word(p, len);
            const long n = num ? strtol(num, nullptr, 10) : 0;
            if (n < 1 || n > static_cast<long>(kNumScenes))
                continue; // its lines are skipped
            scene = &scenes[n - 1];
            scene->used = true;
            scene->latched = 0;
            for (size_t fx = 0; fx < kNumFx; fx++)
                for (size_t i = 0; i < kNumFxParams; i++)
                    scene->params[fx][i] = defaults[fx][i];
            continue;
        }

        if (!scene)
            continue;
        for (size_t fx = 0; fx < kNumFx; fx++)
        {
            if (!Is(word, len, kFxNames[fx]))
                continue;
            const char* latch = Word(p, len);
            if (latch && *latch == '1')
                scene->latched |= static_cast<uint16_t>(1u << fx);
            for (size_t i = 0; i < kNumFxParams; i++)
            {
                const char* num = Word(p, len);
                if (!num)
                    break;
                const float val = static_cast<float>(strtol(num, nullptr, 10)) / kScale;
                scene->params[fx][i] = fclamp(val, 0.f, 1.f);
            }
            break;
        }
    }
    return true;
}

} // namespace chompi
