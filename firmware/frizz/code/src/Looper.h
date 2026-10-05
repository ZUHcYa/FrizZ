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
 *
 *  Speed (step 4): the read head is a frame index plus a fraction, advanced by the speed each
 *  sample and read with 4-point Hermite interpolation. Speed moves in TAPE's ladder of 5ths and
 *  octaves (StepSpeed), glides to each new step like TAPE's default tape slew, and runs in
 *  reverse when negative. While paused, the transport knob scrubs instead (Scrub).
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

// Speed ladder, in semitones: octaves (12k) and fifths above them (12k + 7), as TAPE.
// Top is 2x (+12), bottom is 1/16x (-48); stepping below the bottom flips direction.
static const int kSpeedMaxSemis = 12;
static const int kSpeedMinSemis = -48;
static const float kSpeedSlewCoeff = .0001f; // TAPE's tape-slew glide, ~0.2s

// Scrubbing while paused, as TAPE: the turns counted over each 1/8s set the scrub speed
static const size_t kScrubPeriod = 6000;
static const float kScrubPerTurn = .2f;

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

    /** One step along the 5ths-and-octaves ladder. dir > 0 turns right (faster forward /
     *  slower reverse), dir < 0 turns left. Past 1/16x the direction flips at the same speed. */
    void StepSpeed(int dir)
    {
        if (dir == 0)
            return;

        // turning right means faster when forward, slower when in reverse
        const bool faster = (dir > 0) != reverse_;
        int semis = semis_;

        if (faster)
            semis += (Mod12(semis) == 7) ? 5 : 7;
        else
            semis -= (Mod12(semis) == 7) ? 7 : 5;

        if (semis > kSpeedMaxSemis)
            return; // already at 2x
        if (semis < kSpeedMinSemis)
        {
            reverse_ = !reverse_; // through the slowest step: flip, same speed
            semis = semis_;
        }

        semis_ = semis;
        speed_target_ = (reverse_ ? -1.f : 1.f) * powf(2.f, semis_ / 12.f);
    }

    /** Back to 1x forward */
    void ResetSpeed()
    {
        semis_ = 0;
        reverse_ = false;
        speed_target_ = 1.f;
    }

    /** Transport turns while paused, in encoder detents */
    void Scrub(int turns) { scrub_turns_.fetch_add(turns); }

    // ===== state, readable from the UI =====

    inline State GetState() const { return state_; }
    inline bool IsQuantized() const { return quantized_; }
    /** True once a quantized recording has been told to stop and is finishing its bar */
    inline bool IsClosing() const { return closing_; }
    inline bool CanRecordQuantized() const
    {
        return midi_clock_->HasClock() && midi_clock_->GetTickPeriod() > 0.f;
    }
    /** Current speed target: negative is reverse, 1 is the recorded speed */
    inline float GetSpeed() const { return speed_target_; }

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
        else if (state_ == State::PAUSED)
            UpdateScrub(size);

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

                daisysp::fonepole(speed_, speed_target_, kSpeedSlewCoeff);
                daisysp::fonepole(scrub_, scrub_target_, kSpeedSlewCoeff);
                if (fabsf(scrub_) < .0001f)
                    scrub_ = 0.f;

                {
                    // playing: audible at the ladder speed. Paused: audible only while
                    // scrubbing, at the scrub speed. Erasing: fade out.
                    const bool playing = state_ == State::PLAYING;
                    const bool audible = !erasing_ && (playing || scrub_ != 0.f);
                    daisysp::fonepole(fade_, audible ? 1.f : 0.f, kPlayFadeCoeff);

                    // the fade-out after a pause or erase keeps moving at play speed, so it
                    // doesn't freeze on one sample (a DC thump)
                    if (fading_out_ && (fade_ <= .0001f || scrub_target_ != 0.f))
                        fading_out_ = false;

                    if (fade_ > .0001f)
                    {
                        ReadInterpolated(&out_l[i], &out_r[i]);
                        out_l[i] *= fade_;
                        out_r[i] *= fade_;
                        Advance(playing || fading_out_ ? speed_ : scrub_);
                    }
                    else
                    {
                        out_l[i] = out_r[i] = 0.f;
                    }
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
            // a scrub left over from the last pause mustn't carry into the next one
            scrub_turns_.store(0);
            scrub_ = scrub_target_ = 0.f;
            scrub_count_ = 0;
            if (state_ == State::PLAYING)
            {
                state_ = State::PAUSED;
                fading_out_ = true;
            }
            else if (state_ == State::PAUSED)
            {
                state_ = State::PLAYING;
                fading_out_ = false;
            }
            break;

        case Command::ERASE:
            if (state_ == State::RECORDING)
                Clear(); // nothing is audible yet, so no fade needed
            else if (state_ != State::EMPTY)
            {
                erasing_ = true;
                fading_out_ = true;
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
        play_frac_ = 0.f;
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
        fading_out_ = false;
        length_ = 0;
        write_pos_ = 0;
        play_pos_ = 0;
        play_frac_ = 0.f;
        postroll_ = kXfadeFrames;
        target_length_ = 0;
        fade_ = 0.f;

        ResetSpeed();
        speed_ = 1.f;
        scrub_ = scrub_target_ = 0.f;
        scrub_turns_.store(0);
        scrub_count_ = 0;
    }

    static inline int Mod12(int semis) { return ((semis % 12) + 12) % 12; }

    /** Turns counted over each scrub period set the scrub speed for the next one */
    void UpdateScrub(size_t size)
    {
        scrub_count_ += size;
        if (scrub_count_ < kScrubPeriod)
            return;

        scrub_count_ = 0;
        scrub_target_ = daisysp::fclamp(scrub_turns_.exchange(0) * kScrubPerTurn, -2.f, 2.f);
    }

    /** Moves the read head by speed frames (signed), wrapping around the loop */
    inline void Advance(float speed)
    {
        play_frac_ += speed;
        while (play_frac_ >= 1.f)
        {
            play_frac_ -= 1.f;
            if (++play_pos_ >= length_)
                play_pos_ = 0;
        }
        while (play_frac_ < 0.f)
        {
            play_frac_ += 1.f;
            play_pos_ = play_pos_ == 0 ? length_ - 1 : play_pos_ - 1;
        }
    }

    inline size_t Wrap(size_t frame, int offset) const
    {
        const int64_t f = static_cast<int64_t>(frame) + offset;
        if (f < 0)
            return static_cast<size_t>(f + length_);
        if (f >= static_cast<int64_t>(length_))
            return static_cast<size_t>(f - length_);
        return static_cast<size_t>(f);
    }

    /** 4-point Hermite interpolation around the read head */
    void ReadInterpolated(float* l, float* r) const
    {
        float xl[4], xr[4];
        for (int k = 0; k < 4; k++)
            Read(Wrap(play_pos_, k - 1), &xl[k], &xr[k]);

        *l = Hermite(xl, play_frac_);
        *r = Hermite(xr, play_frac_);
    }

    static inline float Hermite(const float* x, float t)
    {
        const float c0 = x[1];
        const float c1 = .5f * (x[2] - x[0]);
        const float c2 = x[0] - 2.5f * x[1] + 2.f * x[2] - .5f * x[3];
        const float c3 = .5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
        return ((c3 * t + c2) * t + c1) * t + c0;
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
    bool fading_out_ = false; // fading out after a pause or erase, still at play speed

    size_t length_;        // loop length in frames, once closed
    size_t write_pos_;     // frames recorded so far
    size_t play_pos_;      // playback position, whole frames
    float play_frac_;      // playback position, fraction of a frame
    size_t postroll_;      // frames of post-roll written so far
    size_t target_length_; // quantized: where the recording will close, 0 if not yet known
    float fade_;

    // speed, see StepSpeed(). Set from the UI, read by the audio.
    int semis_ = 0;
    bool reverse_ = false;
    volatile float speed_target_ = 1.f;
    float speed_ = 1.f;

    // scrubbing while paused
    std::atomic<int> scrub_turns_{0};
    size_t scrub_count_ = 0;
    float scrub_, scrub_target_;

    uint32_t start_ticks_;
    uint32_t first_tick_time_;
    uint32_t first_tick_count_;
    bool have_first_tick_;
};

} // namespace chompi
