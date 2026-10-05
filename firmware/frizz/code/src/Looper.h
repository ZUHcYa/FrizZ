/** @file Looper.h
 *  @brief The looper core: one stereo loop in SDRAM, recorded from the dry input and played
 *  back into the wet side of the mix. See LOOPER.md for the spec.
 *
 *  Everything here runs in the audio callback. The UI (MainLoop) only posts commands
 *  (StartRecording, StopRecording, TogglePlay, Erase), which are picked up at the start of
 *  the next audio block, so recording starts and ends aligned to the audio block. Lengths are
 *  counted in recorded frames, and MIDI clock tick times are on the same sample clock.
 *
 *  Commands go through a single slot that the audio callback empties once per block, so:
 *   - post at most one command per UI event; a second one in the same block replaces the first
 *   - GetState() only reflects a command from the next audio block on
 *
 *  Loop point: when a recording closes at length L, the input keeps being written for
 *  kXfadeFrames more frames (the post-roll). Reading position p < kXfadeFrames crossfades
 *  from the post-roll (the natural continuation of the loop's tail) into the loop's head, so
 *  the jump from L-1 back to 0 doesn't click, in either direction.
 */
#pragma once
#include <atomic>
#include "daisy.h"
#include "daisysp.h"
#include "MidiClock.h"

using namespace daisy;

namespace chompi
{

// TAPE's loop buffer size: 2:45 of 48kHz stereo int16. The total byte size (31694848) divides
// evenly into 8K, which keeps it aligned to SDRAM pages.
static const size_t kLoopMemSize = 31694848 / 2;      // int16 samples
static const size_t kLoopMaxFrames = kLoopMemSize / 2; // stereo frames

static const size_t kXfadeFrames = 240; // 5ms loop-point crossfade at 48kHz
static const float kPlayFadeCoeff = .002f; // ~10ms fade on play / pause / erase

class Looper
{
public:
    enum class State
    {
        EMPTY,
        RECORDING,
        PLAYING,
        PAUSED,
    };

    Looper() {}
    ~Looper() {}

    void Init(int16_t* mem, MidiClock* midi_clock)
    {
        mem_ = mem;
        midi_clock_ = midi_clock;
        Clear();
    }

    // ===== commands, called from the UI =====

    /** Starts recording if the looper is empty. Quantized needs a running MIDI clock, so check
     *  CanRecordQuantized() first; a quantized start without a clock is ignored. */
    void StartRecording(bool quantized)
    {
        command_.store(quantized ? Command::RECORD_QUANTIZED : Command::RECORD_FREE);
    }
    /** Ends the recording: right away when unquantized, at the end of the bar when quantized */
    void StopRecording() { command_.store(Command::STOP_RECORDING); }
    void TogglePlay() { command_.store(Command::TOGGLE_PLAY); }
    void Erase() { command_.store(Command::ERASE); }

    // ===== state, readable from the UI =====

    inline State GetState() const { return state_; }
    inline bool IsQuantized() const { return quantized_; }
    /** True once a quantized recording has been told to stop and is finishing its bar */
    inline bool IsClosing() const { return closing_; }
    inline bool CanRecordQuantized() const
    {
        return midi_clock_->HasClock() && midi_clock_->GetTickPeriod() > 0.f;
    }
    /** Playback position 0-1 through the loop */
    inline float GetPosition() const
    {
        return length_ > 0 ? static_cast<float>(play_pos_) / static_cast<float>(length_) : 0.f;
    }

    // ===== audio =====

    /** Records from in and plays into out, one block. */
    void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, size_t size)
    {
        HandleCommand();

        if (state_ == State::RECORDING)
            TrackRecordingClock();

        for (size_t i = 0; i < size; i++)
        {
            switch (state_)
            {
            case State::RECORDING:
                Write(write_pos_, in_l[i], in_r[i]);
                write_pos_++;
                out_l[i] = out_r[i] = 0.f;

                if (target_length_ > 0 && write_pos_ >= target_length_)
                    CloseLoop(target_length_);
                else if (write_pos_ >= kLoopMaxFrames - kXfadeFrames)
                    CloseAtMaxLength();
                break;

            case State::PLAYING:
            case State::PAUSED:
                // finish the post-roll: keep writing the input past the loop end
                if (postroll_ < kXfadeFrames)
                {
                    Write(length_ + postroll_, in_l[i], in_r[i]);
                    postroll_++;
                }

                daisysp::fonepole(fade_, state_ == State::PLAYING ? 1.f : 0.f, kPlayFadeCoeff);

                if (fade_ > .0001f)
                {
                    Read(play_pos_, &out_l[i], &out_r[i]);
                    out_l[i] *= fade_;
                    out_r[i] *= fade_;

                    play_pos_++;
                    if (play_pos_ >= length_)
                        play_pos_ = 0;
                }
                else
                {
                    out_l[i] = out_r[i] = 0.f;
                }

                if (erasing_ && fade_ <= .0001f)
                    Clear();
                break;

            case State::EMPTY:
            default:
                out_l[i] = out_r[i] = 0.f;
                break;
            }
        }
    }

private:
    enum class Command : uint8_t
    {
        NONE,
        RECORD_FREE,
        RECORD_QUANTIZED,
        STOP_RECORDING,
        TOGGLE_PLAY,
        ERASE,
    };

    void HandleCommand()
    {
        const Command cmd = command_.exchange(Command::NONE);

        switch (cmd)
        {
        case Command::RECORD_FREE:
        case Command::RECORD_QUANTIZED:
        {
            const bool quantized = cmd == Command::RECORD_QUANTIZED;
            if (state_ != State::EMPTY || (quantized && !CanRecordQuantized()))
                break;

            state_ = State::RECORDING;
            quantized_ = quantized;
            closing_ = false;
            write_pos_ = 0;
            target_length_ = 0;
            start_ticks_ = midi_clock_->GetTicks();
            first_tick_time_ = 0;
            first_tick_count_ = 0;
            have_first_tick_ = false;
            break;
        }

        case Command::STOP_RECORDING:
            if (state_ != State::RECORDING || closing_)
                break;

            if (!quantized_)
                CloseLoop(write_pos_);
            else
            {
                // record to the end of the bar in progress. A press exactly on a bar line
                // starts the next bar (strict rule, LOOPER.md 1.3).
                const float bar = kTicksPerBar * TickPeriod();
                const uint32_t bars = static_cast<uint32_t>(write_pos_ / bar) + 1;
                target_length_ = static_cast<size_t>(bars * bar + .5f);
                closing_ = true;

                if (target_length_ > kLoopMaxFrames - kXfadeFrames)
                    CloseAtMaxLength();
                else if (write_pos_ >= target_length_)
                    CloseLoop(target_length_);
            }
            break;

        case Command::TOGGLE_PLAY:
            if (erasing_)
                break;
            if (state_ == State::PLAYING)
                state_ = State::PAUSED;
            else if (state_ == State::PAUSED)
                state_ = State::PLAYING;
            break;

        case Command::ERASE:
            if (state_ == State::RECORDING)
                Clear(); // nothing is audible yet, so no fade needed
            else if (state_ != State::EMPTY)
            {
                erasing_ = true;
                state_ = State::PAUSED; // fade out, then Clear() in Process
            }
            break;

        case Command::NONE:
        default:
            break;
        }
    }

    /** Snapshots the first tick after the record press, and closes a quantized recording
     *  immediately if the clock goes away (LOOPER.md 1.3). */
    void TrackRecordingClock()
    {
        if (!quantized_)
            return;

        if (!midi_clock_->HasClock())
        {
            CloseLoop(write_pos_);
            return;
        }

        if (!have_first_tick_ && midi_clock_->GetTicks() != start_ticks_)
        {
            have_first_tick_ = true;
            first_tick_time_ = midi_clock_->GetLastTickTime();
            first_tick_count_ = midi_clock_->GetTicks();
        }
    }

    /** Tick period in samples, from the tick span since the record press when there's enough
     *  of it, otherwise the clock's smoothed period (LOOPER.md 2.2) */
    float TickPeriod() const
    {
        const uint32_t ticks = midi_clock_->GetTicks() - first_tick_count_;
        if (have_first_tick_ && ticks >= kTicksPerBeat)
            return static_cast<float>(midi_clock_->GetLastTickTime() - first_tick_time_) / ticks;

        return midi_clock_->GetTickPeriod();
    }

    /** Hit the 2:45 limit: unquantized keeps everything, quantized cuts back to the last
     *  complete bar (LOOPER.md 1.3a) */
    void CloseAtMaxLength()
    {
        size_t length = write_pos_;

        if (quantized_)
        {
            const float bar = kTicksPerBar * TickPeriod();
            const uint32_t bars = static_cast<uint32_t>(length / bar);
            if (bars > 0)
                length = static_cast<size_t>(bars * bar + .5f);
        }

        CloseLoop(length);
    }

    void CloseLoop(size_t length)
    {
        if (length == 0)
        {
            Clear();
            return;
        }

        length_ = length;
        // anything already recorded past the loop end is the start of the post-roll
        postroll_ = write_pos_ > length ? write_pos_ - length : 0;
        if (postroll_ > kXfadeFrames)
            postroll_ = kXfadeFrames;

        play_pos_ = 0;
        fade_ = 1.f; // the loop starts seamlessly, no fade in
        closing_ = false;
        state_ = State::PLAYING;
    }

    void Clear()
    {
        state_ = State::EMPTY;
        quantized_ = false;
        closing_ = false;
        erasing_ = false;
        length_ = 0;
        write_pos_ = 0;
        play_pos_ = 0;
        postroll_ = kXfadeFrames;
        target_length_ = 0;
        fade_ = 0.f;
    }

    inline void Write(size_t frame, float l, float r)
    {
        mem_[frame * 2] = static_cast<int16_t>(f2s16(l));
        mem_[frame * 2 + 1] = static_cast<int16_t>(f2s16(r));
    }

    /** Reads a frame, crossfading the loop head with the post-roll (see the file comment).
     *  Until the post-roll is complete, the frames past what's been written are skipped. */
    inline void Read(size_t frame, float* l, float* r) const
    {
        *l = s162f(mem_[frame * 2]);
        *r = s162f(mem_[frame * 2 + 1]);

        if (frame < kXfadeFrames && frame < postroll_ && frame < length_)
        {
            const float head = static_cast<float>(frame) / kXfadeFrames;
            const size_t tail = length_ + frame;
            *l = *l * head + s162f(mem_[tail * 2]) * (1.f - head);
            *r = *r * head + s162f(mem_[tail * 2 + 1]) * (1.f - head);
        }
    }

    int16_t* mem_;
    MidiClock* midi_clock_;

    std::atomic<Command> command_{Command::NONE};

    State state_;
    bool quantized_;
    bool closing_;
    bool erasing_;

    size_t length_;        // loop length in frames, once closed
    size_t write_pos_;     // frames recorded so far
    size_t play_pos_;      // playback position in frames
    size_t postroll_;      // frames of post-roll written so far
    size_t target_length_; // quantized: where the recording will close, 0 if not yet known
    float fade_;

    uint32_t start_ticks_;
    uint32_t first_tick_time_;
    uint32_t first_tick_count_;
    bool have_first_tick_;
};

} // namespace chompi
