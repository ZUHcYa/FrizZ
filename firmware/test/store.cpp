// store.cpp: checks SceneStore.h's card handling against an SD card in memory (host/fatfs.h):
// the boot read, a .tmp left by a cut save, an unreadable or oversized file kept as .bak, and
// a card first put in after boot, whose scenes and master settings a save mustn't overwrite.
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
static const std::string kMasterBak = "/FRIZZ/frizz_master.bak";

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

static void TestLateCard()
{
    // booted without a card, a scene saved in RAM only
    NewCard(false);
    SceneStore store;
    store.Init(&fs, "");
    SaveInRam(store, 3, .5f);
    Check(!Saves(store), "no card: the save fails");

    // a card with scenes 1 and 2 and master settings put in
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = SceneText(0x3, .25f);
    card.files[kMaster] = "FRIZZ master 1\ncompressor 300000 0 0 0\nmono 1\n";
    Check(Saves(store), "late card: the save works");
    Check(store.scenes[1].used && store.scenes[2].used && store.scenes[3].used,
          "late card: its scenes fill the empty slots");
    Check(UsedIn(card.files[kScenes]) == 0x7 && !card.files.count(kScenesBak),
          "late card, no clash: one file holds all three, no .bak");
    Check(card.files[kMasterBak] == "FRIZZ master 1\ncompressor 300000 0 0 0\nmono 1\n",
          "late card: its master settings are kept as .bak");

    // a clash: the session saved scene 1, the card has its own
    NewCard(false);
    store.Init(&fs, "");
    SaveInRam(store, 1, .5f);
    store.Process();
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    const std::string old = SceneText(0x5, .25f);
    card.files[kScenes] = old;
    float first = 0.f;
    Check(Saves(store), "late card with a clash: the save works");
    Check(card.files[kScenesBak] == old, "... the card's file is kept as .bak");
    Check(UsedIn(card.files[kScenes], 1, &first) == 0x5 && first == .5f,
          "... and the new one has the session's scene 1 and the card's scene 3");

    // the master settings written first: the scenes are still read before any scene save
    NewCard(false);
    store.Init(&fs, "");
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = SceneText(0x2, .25f);
    store.RequestMasterSave();
    store.Process();
    Check(!store.TakeMasterFailed() && card.files.count(kMaster), "late card, master first: written");
    Check(store.scenes[2].used, "... and the card's scenes read");
    SaveInRam(store, 4, .5f);
    Check(Saves(store) && UsedIn(card.files[kScenes]) == 0xa && !card.files.count(kScenesBak),
          "... so the next scene save keeps them");

    // a read-only card: refused, nothing lost, tried again later
    NewCard(false);
    store.Init(&fs, "");
    SaveInRam(store, 1, .5f);
    NewCard(true);
    card.dirs["/FRIZZ"] = true;
    card.files[kScenes] = old;
    card.files[kMaster] = "FRIZZ master 1\n";
    card.read_only = true;
    Check(!Saves(store) && card.files[kScenes] == old, "late read-only card: refused, its file untouched");
}

int main()
{
    TestBoot();
    TestUnreadable();
    TestLateCard();
    return Finish();
}
