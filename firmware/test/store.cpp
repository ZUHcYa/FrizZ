// store.cpp: checks SceneStore.h's card handling against an SD card in memory (host/fatfs.h):
// the boot read, a .tmp left by a cut save, an unreadable or oversized file kept as .bak, and
// a card that couldn't be read at boot, which that session never writes.
// Exits 0 when everything passes. Run by unit.sh store.
#include <cstdio>
#include <string>
#include "check.h"
#include "SceneStore.h"

using namespace chompi;

static FakeCard& card = FakeCard::Get();
static FATFS fs;

static const std::string kScenes = "/FRIZZ/frizz_scenes.txt";
static const std::string kScenesBak = "/FRIZZ/frizz_scenes.bak";
static const std::string kMaster = "/FRIZZ/frizz_master.txt";

static void NewCard(bool present)
{
    card = FakeCard();
    card.present = present;
}

/** A scene file whose used scenes are those in mask (bit s: scene s + 1), each with its first
 *  knob at value */
static std::string SceneText(unsigned mask, float value)
{
    FxScene scenes[kNumScenes];
    for (size_t s = 0; s < kNumScenes; s++)
    {
        scenes[s].used = mask & (1u << s);
        scenes[s].latched = 0;
        for (size_t fx = 0; fx < kNumFx; fx++)
            for (size_t p = 0; p < kNumFxParams; p++)
                scenes[s].params[fx][p] = kFxParams[fx].defaults[p];
        scenes[s].params[0][0] = value;
    }
    static char buf[kSceneFileMax];
    return std::string(buf, FormatScenes(scenes, buf, sizeof(buf)));
}

/** A saved scene in slot (1..4), its first knob at value, as a confirm leaves it */
static void SaveInRam(SceneStore& store, size_t slot, float value)
{
    FxScene& scene = store.scenes[slot];
    scene.used = true;
    scene.latched = 0;
    for (size_t fx = 0; fx < kNumFx; fx++)
        for (size_t p = 0; p < kNumFxParams; p++)
            scene.params[fx][p] = kFxParams[fx].defaults[p];
    scene.params[0][0] = value;
}

static bool Saves(SceneStore& store)
{
    store.RequestSave();
    store.Process();
    return store.GetSaveState() == SceneStore::SaveState::OK;
}

/** The scenes a file on the card holds, as a mask, and scene `slot`'s first knob */
static unsigned UsedIn(const std::string& text, size_t slot = 0, float* value = nullptr)
{
    float defaults[kNumFx][kNumFxParams];
    for (size_t fx = 0; fx < kNumFx; fx++)
        for (size_t p = 0; p < kNumFxParams; p++)
            defaults[fx][p] = kFxParams[fx].defaults[p];
    FxScene scenes[kNumScenes];
    if (!ParseScenes(text.c_str(), defaults, scenes))
        return 0xff;
    unsigned mask = 0;
    for (size_t s = 0; s < kNumScenes; s++)
        mask |= scenes[s].used ? 1u << s : 0;
    if (value && slot)
        *value = scenes[slot - 1].params[0][0];
    return mask;
}

static void TestBoot()
{
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = SceneText(0x3, .25f);
    SceneStore store;
    store.Init(&fs, "");
    Check(store.scenes[1].used && store.scenes[2].used && !store.scenes[3].used,
          "boot: the card's scenes 1 and 2 are read");
    SaveInRam(store, 3, .5f);
    Check(Saves(store) && UsedIn(card.files[kScenes]) == 0x7 && !card.files.count(kScenesBak),
          "boot: a save writes all three, no .bak");

    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files["/FRIZZ/frizz_scenes.tmp"] = SceneText(0x2, .25f);
    store.Init(&fs, "");
    Check(store.scenes[2].used && card.files.count(kScenes) && !card.files.count("/FRIZZ/frizz_scenes.tmp"),
          "boot: a .tmp from a cut save is read and its rename finished");

    NewCard(true);
    card.files["/frizz_scenes.txt"] = SceneText(0x1, .25f);
    store.Init(&fs, "");
    Check(store.scenes[1].used && card.files.count(kScenes), "boot: an old file in the root moves to /FRIZZ");
}

static void TestUnreadable()
{
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = "FRIZZ scenes 2\nsomething else\n";
    SceneStore store;
    store.Init(&fs, "");
    SaveInRam(store, 1, .5f);
    Check(Saves(store) && card.files[kScenesBak] == "FRIZZ scenes 2\nsomething else\n" &&
              UsedIn(card.files[kScenes]) == 0x1,
          "unreadable: kept as .bak on the first save");

    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = "FRIZZ scenes 2\n";
    store.Init(&fs, "");
    card.files.erase(kScenes);
    SaveInRam(store, 1, .5f);
    Check(Saves(store) && UsedIn(card.files[kScenes]) == 0x1,
          "unreadable, then deleted before the save: the save still works");
    SaveInRam(store, 2, .5f);
    Check(Saves(store), "... and the next one too");

    // a valid file padded past what the buffer holds would be read cut short
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    const std::string big = SceneText(0x1, .25f) + std::string(kSceneFileMax, '#');
    card.files[kScenes] = big;
    store.Init(&fs, "");
    Check(!store.scenes[1].used, "too long: not read");
    SaveInRam(store, 2, .5f);
    Check(Saves(store) && card.files[kScenesBak] == big, "... and kept as .bak on the first save");
}

static void TestNotRead()
{
    // the mount failed at boot (FRIZZ itself came from the card, so it's there)
    NewCard(false);
    SceneStore store;
    store.Init(&fs, "");
    SaveInRam(store, 3, .5f);
    Check(!Saves(store), "not read at boot: the save fails");

    // the card answers now, with scenes 1 and 2 and master settings: none of it is touched
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    const std::string scenes = SceneText(0x3, .25f);
    const std::string master = "FRIZZ master 1\ncompressor 300000 0 0 0\nmono 1\n";
    card.files[kScenes] = scenes;
    card.files[kMaster] = master;
    Check(!Saves(store), "not read at boot: a save fails while the card answers too");
    store.RequestMasterSave();
    store.Process();
    Check(store.TakeMasterFailed(), "not read at boot: so does a master save");
    Check(card.files[kScenes] == scenes && card.files[kMaster] == master && card.files.size() == 2,
          "... and the card is untouched");

    // after a power cycle it's read, and written again
    store.Init(&fs, "");
    SaveInRam(store, 3, .5f);
    Check(Saves(store) && UsedIn(card.files[kScenes]) == 0x7, "read at the next boot: written again");
}

int main()
{
    TestBoot();
    TestUnreadable();
    TestNotRead();
    return Finish();
}
