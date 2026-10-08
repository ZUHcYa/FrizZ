/** @file Bench.h
 *  @brief The CPU bench, only in FRIZZ-bench.bin (make BENCH=1; the normal build leaves it out).
 *
 *  After the boot animation it plays a fixed list of segments through the engine on a signal
 *  of its own, 3 s each, and measures the audio callback's load in each, all of it (controls,
 *  UI events, MIDI, the engine), by the system timer as libDaisy's CpuLoadMeter does: the
 *  highest and the mean, as parts of the 0.5 ms a block has. When it's done it writes them to
 *  /FRIZZ/cpu.txt, with the effects that were still working in a segment they're not part of
 *  (a delay or reverb tail: the delay sleeps only after 10 s of silence) and whether the loop
 *  played. So the clean segments come first and the sends last, and it starts only once every
 *  effect rests: the delay works for its first 10 s after power-on, until its tail watch has
 *  heard 10 s of silence.
 *
 *  It measures FRIZZ-bench.bin, whose memory layout isn't FRIZZ.bin's: a crackle that comes
 *  from the layout alone (b5c658c) may show in one and not the other.
 *
 *  LEDs: one key per segment as it's done, white keys left to right, then the dark keys: green
 *  below 80%, amber below 95%, red above; the segment running blinks white. At the end every
 *  panel LED is green (everything below 95%) or red; red blinking means cpu.txt couldn't be
 *  written. Switch off and on to run it again.
 *
 *  The segments are set in the audio callback after the measurement, so setting them doesn't
 *  count; the results are read in MainLoop once the last one is done.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "passthroughEngine.h"
#include "SceneStore.h"
#include "temp_led_stuff.h"

namespace chompi
{

class Bench
{
public:
    static const size_t kSegmentBlocks = 6000; // 3 s at 48 kHz in 24-sample blocks
    static const size_t kRecordBlocks = 8000;  // the loop: 4 s
    static const size_t kKnobBlocks = 500;     // a knob moves every 0.25 s
    static const size_t kSettleBlocks = 30000; // waiting for the effects to rest: 15 s at most

    void Init(float sample_rate, size_t block_size, FATFS* fs, const char* path)
    {
        // ticks of the system timer in one block's time
        ticks_per_block_ = daisy::System::GetTickFreq() * (block_size / sample_rate);
        fs_ = fs;
        path_ = path;
    }

    /** At the very start of the audio callback */
    inline void BlockStart() { start_tick_ = daisy::System::GetTick(); }

    /** The bench's signal into AUX in place of the inputs, while it runs */
    const float* const* Input(const float* const* in, size_t size)
    {
        if (!running_)
            return in;
        for (size_t i = 0; i < size && i < kMaxBlock; i++)
        {
            // a 110 Hz saw, a 2 kHz sine gated at 4 Hz and a little noise
            saw_ += 110.f / 48000.f;
            if (saw_ >= 1.f)
                saw_ -= 1.f;
            sine_ += 2000.f / 48000.f;
            if (sine_ >= 1.f)
                sine_ -= 1.f;
            gate_ = (gate_ + 1) % 12000;
            const float s = .25f * (2.f * saw_ - 1.f)
                            + (gate_ < 6000 ? .2f * sinf(6.2831853f * sine_) : 0.f)
                            + .02f * (Random() * 2.f - 1.f);
            in_l_[i] = s;
            in_r_[i] = s * .9f;
        }
        inputs_[0] = in[0];
        inputs_[1] = in[1];
        inputs_[2] = in_l_;
        inputs_[3] = in_r_;
        return inputs_;
    }

    /** At the very end of the audio callback: the measurement, then the next step */
    void BlockEnd(PassthroughEngine& engine)
    {
        const float load = static_cast<float>(daisy::System::GetTick() - start_tick_)
                           / ticks_per_block_;
        if (!started_)
        {
            started_ = true;
            running_ = true;
            settling_ = true;
            for (size_t fx = 0; fx < kNumFx; fx++)
                engine.SetFxOn(fx, false);
            return;
        }
        if (!running_)
            return;
        if (settling_)
        {
            // until every effect rests: the delay keeps working for 10 s after power-on, until
            // its tail watch has heard 10 s of silence
            bool resting = true;
            for (size_t fx = 0; fx < kNumFx; fx++)
                resting &= engine.FxResting(fx);
            if (resting || ++blocks_ >= kSettleBlocks)
            {
                settling_ = false;
                Begin(engine, 0);
            }
            return;
        }

        max_[segment_] = fmaxf(max_[segment_], load);
        sum_ += load;
        blocks_++;
        const Segment& seg = kSegments[segment_];
        if (!(seg.flags & (STRESS | SCENE4 | RECORD)) && blocks_ % kKnobBlocks == 0)
            TurnAKnob(engine);
        size_t length = kSegmentBlocks;
        if (seg.flags & RECORD)
            length = kRecordBlocks;
        if (blocks_ < length)
            return;

        mean_[segment_] = static_cast<float>(sum_ / blocks_);
        // what still worked without being part of the segment, and the loop
        for (size_t fx = 0; fx < kNumFx; fx++)
            if (!(seg.fx & (1u << fx)) && !engine.FxResting(fx))
                awake_[segment_] |= static_cast<uint16_t>(1u << fx);
        if (seg.flags & RECORD)
        {
            engine.looper.StopRecording();
            loop_frames_ = engine.looper.GetLength(); // 0 until the stop lands: read below
        }
        if (seg.flags & LOOP)
        {
            loop_frames_ = engine.looper.GetLength();
            if (engine.looper.GetState() != Looper::State::PLAYING)
                loop_missing_ = true;
        }
        Begin(engine, segment_ + 1);
    }

    /** In MainLoop: writes cpu.txt once the last segment is done */
    void Process()
    {
        if (running_ || !started_ || written_)
            return;
        written_ = true;
        stored_ = Write();
    }

    /** In MainLoop, after the UI has drawn: the bench's LEDs over it */
    void DrawLeds(uint32_t now)
    {
        if (!started_)
            return;
        for (size_t s = 0; s < kNumSegments && s < kNumKeyLeds; s++)
        {
            float r = 0.f, g = 0.f, b = 0.f;
            if (s < segment_ || !running_)
                Grade(max_[s], r, g, b);
            else if (s == segment_ && !settling_)
                r = g = b = (now / 150) % 2 ? .6f : 0.f;
            SetSmtLedFloat(kKeyLeds[s], r, g, b);
        }
        float worst = 0.f;
        for (size_t s = 0; s < kNumSegments; s++)
            worst = fmaxf(worst, max_[s]);
        for (int i = 0; i < kNumPthLeds - 2 * kPorchSize; i++)
        {
            float r = 0.f, g = 0.f, b = 0.f;
            if (running_)
                r = g = b = i == 0 ? .3f : 0.f; // CHOMPI dimly: the bench is running
            else if (written_ && !stored_)
                r = (now / 250) % 2 ? 1.f : 0.f;
            else if (written_)
                (worst < kRed ? g : r) = 1.f;
            SetPthLedFloat(i, r, g, b);
        }
        fill_led_data();
    }

private:
    // what a segment plays
    enum Flags : uint8_t
    {
        LOOP = 1,     // a loop plays (recorded just before the first of these)
        STRESS = 2,   // the harness's STRESS: every feedback at the top
        SCENE4 = 4,   // PR #7's scene 4: shifter +7, folder, crusher, slicer, compressor at 1
        COMP = 8,     // the compressor's amount at 1
        RECORD = 16,  // records the loop, 4 s
    };
    struct Segment
    {
        const char* name;
        uint16_t fx; // bit FxId: on
        uint8_t flags;
    };
    static constexpr uint16_t Bit(int fx) { return static_cast<uint16_t>(1u << fx); }
    static const uint16_t kAllFx = (1u << kNumFx) - 1;
    // everything but the tape stop, which would silence the rest
    static const uint16_t kEverything = kAllFx & ~(1u << FX_TAPESTOP);
    static const uint16_t kInserts = kEverything & ~(1u << FX_DELAY) & ~(1u << FX_REVERB);
    static const uint16_t kScene4 = (1u << FX_SHIFTER) | (1u << FX_FOLDER) | (1u << FX_CRUSHER)
                                    | (1u << FX_SLICER);

    static constexpr size_t kNumSegments = 22;
    static const Segment kSegments[kNumSegments];

    static const size_t kMaxBlock = 48;
    static const int kNumKeyLeds = 25;
    // the keys' LEDs: the white keys left to right (FxSlots.h's key_led), then the dark keys
    static constexpr uint8_t kKeyLeds[kNumKeyLeds] = {24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14,
                                                       13, 12, 11, 10, 0,  1,  2,  3,  4,  5,
                                                       6,  7,  8,  9};
    static constexpr float kAmber = .8f, kRed = .95f;

    static void Grade(float load, float& r, float& g, float& b)
    {
        b = 0.f;
        if (load < kAmber)
            r = 0.f, g = 1.f;
        else if (load < kRed)
            r = 1.f, g = .5f;
        else
            r = 1.f, g = 0.f;
    }

    void Begin(PassthroughEngine& engine, size_t s)
    {
        segment_ = s;
        blocks_ = 0;
        sum_ = 0.;
        if (s >= kNumSegments)
        {
            running_ = false;
            for (size_t fx = 0; fx < kNumFx; fx++)
                engine.SetFxOn(fx, false);
            engine.SetMix(0.f);
            return;
        }
        const Segment& seg = kSegments[s];
        engine.SetMainGain(.75f);
        engine.SetInputGain(.75f);
        engine.SetCompParam(MasterComp::kAmount, seg.flags & (COMP | SCENE4) ? 1.f : .3f);
        for (size_t fx = 0; fx < kNumFx; fx++)
            for (size_t p = 0; p < kNumFxParams; p++)
                engine.SetFxParam(fx, p, .5f);
        if (seg.flags & STRESS)
        {
            // the harness's STRESS (test/harness.cpp): the resonator's loop at its most extreme
            engine.SetFxParam(FX_RESONATOR, 1, 1.f);
            engine.SetFxParam(FX_RESONATOR, 2, 1.f);
            engine.SetFxParam(FX_FILTER, 0, .2f);
            engine.SetFxParam(FX_FILTER, 1, 1.f);
            engine.SetFxParam(FX_FLANGER, 2, .5f);
            engine.SetFxParam(FX_FLANGER, 1, 1.f);
            engine.SetFxParam(FX_SHIFTER, 1, 1.f);
            engine.SetFxParam(FX_DELAY, 1, 1.f);
            engine.SetFxParam(FX_REVERB, 0, 1.f);
            engine.SetFxParam(FX_REVERB, 3, 1.f);
        }
        if (seg.flags & SCENE4)
            engine.SetFxParam(FX_SHIFTER, 0, .5f + 7.f / 24.f);
        for (size_t fx = 0; fx < kNumFx; fx++)
            engine.SetFxOn(fx, seg.fx & (1u << fx));
        if (seg.flags & RECORD)
        {
            engine.SetMix(0.f);
            engine.looper.StartRecording(false);
        }
        else
            engine.SetMix(seg.flags & LOOP ? .5f : 0.f);
    }

    void TurnAKnob(PassthroughEngine& engine)
    {
        const uint16_t on = kSegments[segment_].fx;
        if (!on)
            return;
        size_t fx;
        do
            fx = static_cast<size_t>(Random() * kNumFx) % kNumFx;
        while (!(on & (1u << fx)));
        engine.SetFxParam(fx, static_cast<size_t>(Random() * kNumFxParams) % kNumFxParams,
                          Random());
    }

    inline float Random()
    {
        lcg_ = lcg_ * 1664525u + 1013904223u;
        return static_cast<float>(lcg_ >> 8) / 16777216.f;
    }

    // a load as a percentage in 6 characters, "  87.3": the firmware's printf has no floats
    static void Percent(float load, char* out)
    {
        int tenths = static_cast<int>(load * 1000.f + .5f);
        if (tenths > 99999)
            tenths = 99999;
        int whole = tenths / 10;
        out[5] = static_cast<char>('0' + tenths % 10);
        out[4] = '.';
        for (int i = 3; i >= 0; i--)
        {
            out[i] = whole > 0 || i == 3 ? static_cast<char>('0' + whole % 10) : ' ';
            whole /= 10;
        }
    }

    struct Text
    {
        char buf[3072];
        size_t pos = 0;
        void Put(const char* s)
        {
            while (*s && pos + 1 < sizeof(buf))
                buf[pos++] = *s++;
        }
        void Pad(size_t to)
        {
            while (pos < to && pos + 1 < sizeof(buf))
                buf[pos++] = ' ';
        }
        void Load(float load)
        {
            char p[7] = {};
            Percent(load, p);
            Put(p);
        }
    };

    bool Write()
    {
        static Text t;
        t.pos = 0;
        t.Put("FRIZZ cpu bench, built " __DATE__ " " __TIME__ "\n");
        t.Put("# of the 0.5 ms a block has; measured in FRIZZ-bench.bin, whose memory layout\n");
        t.Put("# isn't FRIZZ.bin's\n");
        t.Put("# segment           max %  mean %  still working\n");
        float worst = 0.f;
        size_t worst_at = 0;
        for (size_t s = 0; s < kNumSegments; s++)
        {
            const size_t line = t.pos;
            t.Put(kSegments[s].name);
            t.Pad(line + 18);
            t.Load(max_[s]);
            t.Put("  ");
            t.Load(mean_[s]);
            if (awake_[s])
                t.Pad(line + 35);
            for (size_t fx = 0; fx < kNumFx; fx++)
                if (awake_[s] & (1u << fx))
                {
                    t.Put(" ");
                    t.Put(kFxNames[fx]);
                }
            t.Put("\n");
            if (max_[s] > worst)
            {
                worst = max_[s];
                worst_at = s;
            }
        }
        t.Put("# worst: ");
        t.Put(kSegments[worst_at].name);
        t.Put("\n# loop: ");
        char len[8] = {};
        Percent(static_cast<float>(loop_frames_) / 48000.f / 100.f, len); // seconds, 1 decimal
        t.Put(len);
        t.Put(loop_missing_ || loop_frames_ == 0 ? " s, NOT playing in every loop segment\n"
                                                 : " s, playing in every loop segment\n");

        if (f_mount(fs_, path_, 1) != FR_OK)
            return false;
        EnterFrizzDir(); // a mount goes back to the root; SceneStore's paths are relative
        static FIL file;
        if (f_open(&file, "/FRIZZ/cpu.txt", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            return false;
        UINT written = 0;
        const FRESULT res = f_write(&file, t.buf, static_cast<UINT>(t.pos), &written);
        return f_close(&file) == FR_OK && res == FR_OK && written == t.pos;
    }

    float ticks_per_block_ = 1.f;
    uint32_t start_tick_ = 0;
    FATFS* fs_ = nullptr;
    const char* path_ = nullptr;
    volatile bool started_ = false, running_ = false;
    bool settling_ = false;
    bool written_ = false, stored_ = false;
    volatile size_t segment_ = 0;
    size_t blocks_ = 0;
    double sum_ = 0.;
    float max_[kNumSegments] = {};
    float mean_[kNumSegments] = {};
    uint16_t awake_[kNumSegments] = {};
    size_t loop_frames_ = 0;
    bool loop_missing_ = false;
    uint32_t lcg_ = 12345;
    float saw_ = 0.f, sine_ = 0.f;
    uint32_t gate_ = 0;
    float in_l_[kMaxBlock], in_r_[kMaxBlock];
    const float* inputs_[4];
};

// The clean segments first, the sends last: a delay or reverb tail would run on into the next
// segments (the delay sleeps only after 10 s of silence). The delay before the reverb, so the
// loop with the delay, the case that crackled, is measured on its own
const Bench::Segment Bench::kSegments[Bench::kNumSegments] = {
    {"idle", 0, 0},
    {"freezer", Bench::Bit(FX_FREEZER), 0},
    {"shifter", Bench::Bit(FX_SHIFTER), 0},
    {"folder", Bench::Bit(FX_FOLDER), 0},
    {"crusher", Bench::Bit(FX_CRUSHER), 0},
    {"filter", Bench::Bit(FX_FILTER), 0},
    {"flanger", Bench::Bit(FX_FLANGER), 0},
    {"resonator", Bench::Bit(FX_RESONATOR), 0},
    {"slicer", Bench::Bit(FX_SLICER), 0},
    {"warble", Bench::Bit(FX_WARBLE), 0},
    {"tapestop", Bench::Bit(FX_TAPESTOP), 0},
    {"compressor", 0, Bench::COMP},
    {"inserts", Bench::kInserts, 0},
    {"recording", 0, Bench::RECORD},
    {"loop", 0, Bench::LOOP},
    {"loop+inserts", Bench::kInserts, Bench::LOOP},
    {"loop+delay", Bench::Bit(FX_DELAY), Bench::LOOP},
    {"loop+scene4+delay", Bench::kScene4 | Bench::Bit(FX_DELAY), Bench::LOOP | Bench::SCENE4},
    {"loop+reverb", Bench::Bit(FX_REVERB), Bench::LOOP},
    {"everything", Bench::kEverything, Bench::COMP},
    {"stress", Bench::kEverything, Bench::STRESS | Bench::COMP},
    {"loop+everything", Bench::kEverything, Bench::LOOP | Bench::COMP},
};
constexpr uint8_t Bench::kKeyLeds[];

} // namespace chompi
