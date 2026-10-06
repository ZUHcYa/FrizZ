/** @file SceneStore.h
 *  @brief The FX scenes (FxScenes.h) and their file on the SD card, frizz_scenes.txt at the
 *  root. Read once at boot; written only when the play page saves, copies or deletes one,
 *  never on a recall, so a performance doesn't touch the card.
 *
 *  The write runs in MainLoop (Process), not in the button handler: the audio callback runs
 *  the UI during boot. It goes to frizz_scenes.tmp first, which then replaces the old file,
 *  so a power cut mid-write leaves one or the other; reading falls back to the .tmp.
 *
 *  Without a card the scenes still work, in RAM only, and are gone at power-off. A write that
 *  fails is reported (GetSaveState) and the next save tries again.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "FxScenes.h"
#include "FxParams.h"

namespace chompi
{

static const char kSceneFile[] = "frizz_scenes.txt";
static const char kSceneTmpFile[] = "frizz_scenes.tmp";

class SceneStore
{
public:
    enum class SaveState
    {
        IDLE,    // nothing saved yet
        PENDING, // requested, the next Process writes it
        OK,      // the last save is on the card
        FAILED,  // the last save isn't: no card, or the write failed
    };

    /** After f_mount, with whether it worked; before audio starts */
    void Init(bool mounted)
    {
        mounted_ = mounted;
        save_state_ = SaveState::IDLE;
        for (size_t s = 0; s < kNumScenes; s++)
            scenes[s].used = false;

        // for the effects a saved scene leaves out
        float defaults[kNumFx][kNumFxParams];
        for (size_t fx = 0; fx < kNumFx; fx++)
            for (size_t p = 0; p < kNumFxParams; p++)
                defaults[fx][p] = kFxParams[fx].defaults[p];

        if (mounted_ && !Load(kSceneFile, defaults))
            Load(kSceneTmpFile, defaults);
    }

    /** From the play page after a change: the next Process writes the file */
    inline void RequestSave() { save_state_ = SaveState::PENDING; }

    /** How the last requested save went, for the play page's confirmation */
    inline SaveState GetSaveState() const { return save_state_; }

    /** From MainLoop */
    void Process()
    {
        if (save_state_ != SaveState::PENDING)
            return;
        save_state_ = mounted_ && Save() ? SaveState::OK : SaveState::FAILED;
    }

    FxScene scenes[kNumScenes];

private:
    bool Load(const char* name, const float (*defaults)[kNumFxParams])
    {
        if (f_open(&file_, name, FA_READ) != FR_OK)
            return false;
        UINT len = 0;
        const FRESULT res = f_read(&file_, buf_, kSceneFileMax - 1, &len);
        f_close(&file_);
        if (res != FR_OK)
            return false;
        buf_[len] = '\0';
        return ParseScenes(buf_, defaults, scenes);
    }

    bool Save()
    {
        const size_t len = FormatScenes(scenes, buf_, kSceneFileMax);
        if (len == 0)
            return false;

        if (f_open(&file_, kSceneTmpFile, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            return false;
        UINT written = 0;
        const FRESULT res = f_write(&file_, buf_, len, &written);
        if (f_close(&file_) != FR_OK || res != FR_OK || written != len)
            return false;

        // f_rename won't replace a file
        const FRESULT del = f_unlink(kSceneFile);
        if (del != FR_OK && del != FR_NO_FILE)
            return false;
        return f_rename(kSceneTmpFile, kSceneFile) == FR_OK;
    }

    FIL file_;
    // FatFs reads whole sectors straight into it by DMA, so on a cache line of its own
    alignas(32) char buf_[kSceneFileMax];
    bool mounted_;
    volatile SaveState save_state_;
};

} // namespace chompi
