/** @file granularDelay.h
 *  @brief TEMPO 1.0's tempo-synced delay, ported to FRIZZ as the delay send (see FxDelay.h).
 *
 *  Changes from TEMPO:
 *   - no clockManager: the engine sets the tempo (SetTempo) and sends 12 PPQN pulses
 *     (ClockPulse) and 8th-note edges (setClockEdge), from TempoClock.h via FxDelay.h.
 *     TEMPO's edges came from its arpeggiator step instead.
 *   - TEMPO's bipolar main knob (division by distance from centre, random side / shimmer side,
 *     on/off dead zone) is split into setDivision and setRandom; the delay is always on and
 *     wet_amt_ stays at 1, so the return level is applied outside and doesn't shorten tails.
 *   - no buffer mute option and no getColors (the LEDs are drawn in NormalPage).
 *   - no freeze (TEMPO's buffer lock), so no frozen buffer.
 */
#pragma once
#include "daisysp.h"
#include "FxCommon.h"

using namespace daisysp;

constexpr float delayDivs[9] = {1.f/8.f, 1.f/6.f, 1.f/4.f, 1.f/3.f, 3.f/8.f, 1.f/2.f, 3.f/4.f, 1.f, 2.f};
constexpr uint32_t kMaxCrossfadeSamps = 256;       // a division change
constexpr uint32_t kMaxEventCrossfadeSamps = 1024; // a random event's fade in and out

constexpr float delayStereoOffsetLeft = 960.f;
constexpr float delayStereoOffsetRight = 480.f;

namespace chompi {
    inline void getSample(float *buffer, float read_head, float *out_l, float *out_r, size_t buffer_size) {
        float right_read_head = read_head - delayStereoOffsetRight;
        if (right_read_head < 0.f) {
            right_read_head += static_cast<float>(buffer_size);
        }
        float left_read_head = read_head - delayStereoOffsetLeft;
        if (left_read_head < 0.f) {
            left_read_head += static_cast<float>(buffer_size);
        }

        size_t i0_l = static_cast<int>(left_read_head);
        if (i0_l >= buffer_size) {
            i0_l -= buffer_size;
        }
        size_t i1_l = (i0_l + 1);
        if (i1_l >= buffer_size) {
            i1_l -= buffer_size;
        }

        size_t i0_r = static_cast<int>(right_read_head);
        if (i0_r >= buffer_size) {
            i0_r -= buffer_size;
        }
        size_t i1_r = (i0_r + 1);
        if (i1_r >= buffer_size) {
            i1_r -= buffer_size;
        }

        float frac = left_read_head - static_cast<float>(i0_l);

        // Read interleaved stereo
        float a_l = buffer[i0_l * 2];
        float b_l = buffer[i1_l * 2];

        float a_r = buffer[i0_r * 2 + 1];
        float b_r = buffer[i1_r * 2 + 1];

        *out_l += (a_l + frac * (b_l - a_l));
        *out_r += (a_r + frac * (b_r - a_r));
    }

    inline float fast_rsqrt(float x) {
        union { float f; uint32_t i; } conv;
        conv.f = x;
        conv.i = 0x5f3759dfU - (conv.i >> 1);
        float y = conv.f;
        // one Newton step to improve accuracy
        y = y * (1.5f - 0.5f * x * y * y);
        return y;
    }

    inline float fast_sqrt(float x) {
        if (x <= 0.f) return 0.f;
        return x * fast_rsqrt(x); // sqrt(x) ≈ x * (1/sqrt(x))
    }
};

class delayVoice {
    public:
    delayVoice() {};
    ~delayVoice() {};

    enum delayEvent {
        RETRIG,
        REVERSE,
        PITCH_UP,
        PITCH_DOWN,
        NONE
    };

    /** active: the voice that starts out reading */
    void Init(float *buffer, size_t buffer_size, bool active) {
        buffer_ = buffer;
        buffer_size_ = buffer_size;
        write_head_ = 0;
        read_head_ = 0.f;
        crossfade_env_ = 1.f;
        delay_samples_ = 0.f;
        curEvent = nextEvent = NONE;
        curPan = nextPan = 0.f;
        event_crossfade_counter_ = 0;
        active_ = active;
        fading_in_ = fading_out_ = false;
        div_pos_ = 8;
        div_read_head_ = div_delay_samples_ = 0.f;
        div_crossfade_ = false;
        div_crossfade_counter_ = 0;
        div_env_ = 1.f;
    }

    void updateTempo(float delay_samples, uint32_t write_head) {
        if (!div_crossfade_) {
            delay_samples_ = delay_samples;
        }
        write_head_ = write_head;
    }

    void startFadeIn() {
        if (fading_in_ || fading_out_ || active_) {
            return;
        }
        active_ = true;
        fading_in_ = true;
        event_crossfade_counter_ = 0;
        curEvent = nextEvent;
        nextEvent = NONE; // Until otherwise changed
        curPan = nextPan;
        nextPan = 0.f;
        if (curEvent == delayEvent::PITCH_UP && div_pos_ < 5) {

            read_head_ = write_head_ - delay_samples_ * (0.5f / delayDivs[div_pos_]);
        }
        else {
            read_head_ = write_head_ - delay_samples_;
        }
        if (read_head_ < 0.f) {
            read_head_ += static_cast<float>(buffer_size_);
        }
    }

    void startFadeOut() {
        if (fading_in_ || fading_out_ || !active_) {
            return;
        }
        fading_out_ = true;
        event_crossfade_counter_ = kMaxEventCrossfadeSamps;
    }

    void setNextEvent(size_t event, float pan) {
        nextEvent = static_cast<delayEvent>(event);
        nextPan = pan;
    }

    bool setDivCrossfade(float new_read_head, float new_delay_samples, size_t new_div) {
        if (active_) {
            if (div_crossfade_) {
                return false;
            }
            div_read_head_ = new_read_head;
            div_delay_samples_ = new_delay_samples;
            div_crossfade_counter_ = kMaxCrossfadeSamps;
            div_crossfade_ = true;
        }
        else {
            read_head_ = new_read_head;
        }
        div_pos_ = new_div;
        return true;
    }

    void Read(float *out_l, float *out_r) {

        if (!active_) {
            return;
        }

        crossfade_env_ = 1.f;
        div_env_ = 1.f;

        float panL = chompi::fast_sqrt(0.5f * (1.f - curPan));
        float panR = chompi::fast_sqrt(0.5f * (1.f + curPan));

        if (fading_in_) {
            event_crossfade_counter_++;
            if (event_crossfade_counter_ > kMaxEventCrossfadeSamps) {
                event_crossfade_counter_ = kMaxEventCrossfadeSamps;
                fading_in_ = false;
            }
            float phase = static_cast<float>(event_crossfade_counter_) / static_cast<float>(kMaxEventCrossfadeSamps);
            crossfade_env_ = sinf(phase * PI_F * 0.5f);
        }
        else if (fading_out_) {
            event_crossfade_counter_--;
            if (event_crossfade_counter_ < 1) {
                fading_out_ = false;
                active_ = false;
            }
            float phase = static_cast<float>(event_crossfade_counter_) / static_cast<float>(kMaxEventCrossfadeSamps);
            crossfade_env_ = sinf(phase * PI_F * 0.5f);
        }

        switch (curEvent) {
            case NONE: {
                read_head_ = write_head_ - delay_samples_;
            }
            break;
            case RETRIG: {
                read_head_ = static_cast<float>(write_head_) - delay_samples_ - delay_samples_ * .125; // Might have to do target
            }
            break;
            case REVERSE: {
                read_head_ -= 1.f;
            }
            break;
            case PITCH_UP: {
                read_head_ += 2.f;
            }
            break;
            case PITCH_DOWN: {
                read_head_ += .5f;
            }
            break;
        };
        if (read_head_ < 0.f) {
            read_head_ += static_cast<float>(buffer_size_);
        }
        if (read_head_ >= static_cast<float>(buffer_size_)) {
            read_head_ -= static_cast<float>(buffer_size_);
        }

        float sig_l = 0.f;
        float sig_r = 0.f;

        if (div_crossfade_) {
            div_crossfade_counter_--;
            div_env_ = static_cast<float>(div_crossfade_counter_) / static_cast<float>(kMaxCrossfadeSamps);
            if (div_crossfade_counter_ == 0) {
                read_head_ = div_read_head_;
                delay_samples_ = div_delay_samples_;
                div_crossfade_ = false;
                div_env_ = 1.f;
            }
        }

        chompi::getSample(buffer_, read_head_, &sig_l, &sig_r, buffer_size_);
        *out_l += sig_l * crossfade_env_ * div_env_ * panL;
        *out_r += sig_r * crossfade_env_ * div_env_ * panR;

        if (div_crossfade_) {

            switch (curEvent) {
                case NONE: {
                    div_read_head_ += 1.f;
                }
                break;
                case RETRIG: {
                    div_read_head_ += 1.f;
                }
                break;
                case REVERSE: {
                    div_read_head_ -= 1.f;
                }
                break;
                case PITCH_UP: {
                    div_read_head_ += 2.f;
                }
                break;
                case PITCH_DOWN: {
                    div_read_head_ += .5f;
                }
                break;
            }
            if (div_read_head_ < 0.f) {
                div_read_head_ += static_cast<float>(buffer_size_);
            }
            if (div_read_head_ >= static_cast<float>(buffer_size_)) {
                div_read_head_ -= static_cast<float>(buffer_size_);
            }

            float c_sig_l = 0.f;
            float c_sig_r = 0.f;
            chompi::getSample(buffer_, div_read_head_, &c_sig_l, &c_sig_r, buffer_size_);
            *out_l += c_sig_l * crossfade_env_ * (1.f - div_env_) * panL;
            *out_r += c_sig_r * crossfade_env_ * (1.f - div_env_) * panR;
        }
    }

    float *buffer_;
    size_t buffer_size_;
    uint32_t write_head_;
    
    float read_head_;
    float crossfade_env_;
    float delay_samples_;
    delayEvent curEvent, nextEvent;
    float curPan, nextPan;
    uint32_t event_crossfade_counter_;
    bool active_;
    bool fading_in_, fading_out_;
    size_t div_pos_;

    float div_read_head_, div_delay_samples_;
    bool div_crossfade_;
    size_t div_crossfade_counter_;
    float div_env_;
};

/** TEMPO's delay, without its freeze: one interleaved stereo buffer, two voices that crossfade
 *  between the plain repeat and the random events */
class granularDelay {
    public:
    granularDelay() {};
    ~granularDelay() {};

    using delayEvent = delayVoice::delayEvent;

    void Init(float *buff, size_t delaySize) {
        buffer_ = buff;
        buffer_size_ = delaySize;

        write_head_ = 0;
        SetTempo(120);
        delay_samples_ = delay_samples_target_ = static_cast<float>(interval_us_) * .192f * delayDivs[2];
        cur_sig_l_ = cur_sig_r_ = 0.f;

        division_ = 2;
        shimmer_ = false;
        alt_control_ = 0.f;
        delay_div_position_ = 2;

        setFeedback(.3f);

        clock_edge_ = false;
        since_edge_ = 0;
        rng_.Seed(0x6C8E9CF5u);
        event_type_[0] = event_type_[1] = delayVoice::NONE;
        curIdx = 0;
        nextIdx = 1;
        for (size_t i = 0; i < 2; ++i) {
            myVoices[i].Init(buffer_, buffer_size_, i == curIdx);
        }
    }

    void write(float in_l, float in_r) {
        size_t idx = write_head_ * 2;
        buffer_[idx] = in_l + cur_sig_l_;
        buffer_[idx + 1] = in_r + cur_sig_r_;

        write_head_ = (write_head_ + 1) % buffer_size_;
    }

    void read(float* out_l, float* out_r) {
        *out_l = *out_r = 0.f;

        const size_t t_ = tempo_;
        const size_t new_interval = division_;

        if (new_interval != delay_div_position_) {
            float new_delay_samps = (60000.f / t_) * 4.f * delayDivs[new_interval] * 48.f;
            float new_read_head = write_head_ - new_delay_samps;
            if (new_read_head < 0.f) {
                new_read_head += static_cast<float>(buffer_size_);
            }
            bool success = true;
            for (size_t i = 0; i < 2; ++i) {
                if (!myVoices[i].setDivCrossfade(new_read_head, new_delay_samps, new_interval)) {
                    success = false;
                }
            }
            if (success) {
                delay_samples_ = new_delay_samps;
                delay_div_position_ = new_interval;
            }
        }

        delay_samples_target_ = static_cast<float>(interval_us_) * .192f * delayDivs[delay_div_position_];
        fonepole(delay_samples_, delay_samples_target_, .001f);

        for (size_t i = 0; i < 2; ++i) {
            myVoices[i].updateTempo(delay_samples_, write_head_);
        }

        // An event runs to the next 8th-note edge. On a slowed-down loop the edges come much
        // later than the tempo (clamped to 50 BPM) says, and a reverse or pitched head would
        // run off the buffer: past an 8th at the tempo and its fade, the event ends as on an
        // edge without one
        bool cut_short = false;
        if (++since_edge_ > EventSamples() && event_type_[1] != delayVoice::NONE && !clock_edge_) {
            clock_edge_ = true;
            cut_short = true;
        }

        // every 8th note: maybe a random event, on the voice that isn't playing
        if (clock_edge_) {
            since_edge_ = 0;
            event_type_[0] = event_type_[1];
            bool random_event = !cut_short && rng_.Uniform() < (0.5f * alt_control_);
            if (random_event) {
                curIdx = nextIdx;
                nextIdx = (curIdx + 1) % 2;
                float pan = 0.f;
                if (!shimmer_) {
                    event_type_[1] = static_cast<delayEvent>(rng_.Next() % 4);
                    if (event_type_[1] == delayVoice::REVERSE && !ReverseFits()) {
                        event_type_[1] = delayVoice::RETRIG;
                    }
                }
                else {
                    event_type_[1] = delayVoice::PITCH_UP;
                    pan = randomPan();
                }
                myVoices[curIdx].setNextEvent(event_type_[1], pan);
            }
            else {
                event_type_[1] = delayVoice::NONE;
                if (event_type_[0] != delayVoice::NONE) {
                    curIdx = nextIdx;
                    nextIdx = (curIdx + 1) % 2;
                    myVoices[curIdx].setNextEvent(delayVoice::NONE, 0.f);
                }
            }
            clock_edge_ = false;
        }

        if (event_type_[0] != delayVoice::NONE || event_type_[1] != delayVoice::NONE) {
            myVoices[curIdx].startFadeIn(); // They manage themselves
            myVoices[nextIdx].startFadeOut();
        }

        for (size_t i = 0; i < 2; ++i) {
            myVoices[i].Read(out_l, out_r);
        }

        cur_sig_l_ = *out_l * delay_feedback_amt_;
        cur_sig_r_ = *out_r * delay_feedback_amt_;
    }

    /** Whether a reverse event fits the buffer. Its read head runs away from the write head
     *  at 2 samples a sample, for up to an 8th note plus its fades; at a slow tempo on a long
     *  division it would lap the write head and jump into what was just written */
    bool ReverseFits() const {
        const float edge = 60.f * 48000.f / (static_cast<float>(tempo_) * 2.f);
        const float run = edge + 2.f * static_cast<float>(kMaxEventCrossfadeSamps);
        return delay_samples_ + 2.f * run + 2.f < static_cast<float>(buffer_size_);
    }

    /** The longest an event runs: an 8th note at the tempo, plus a fade */
    uint32_t EventSamples() const {
        return static_cast<uint32_t>(60.f * 48000.f / (static_cast<float>(tempo_) * 2.f))
               + kMaxEventCrossfadeSamps;
    }

    float randomPan() {
        float effective_range = 0.5f + (alt_control_ * 0.5f); // Range: [0.5, 1.0]
        float r = static_cast<float>(static_cast<int>(rng_.Next() % 2001) - 1000) / 1000.f; // [-1.0, 1.0]
        return r * effective_range;
    }

    void setFeedback(float val) {
        delay_feedback_amt_ = val * .975f;
    }

    /** Beats per minute; the engine keeps it in the range where 2 bars fit the buffer */
    void SetTempo(int bpm) {
        tempo_ = bpm;
        interval_us_ = 60000000 / bpm;
    }

    /** Index into delayDivs: 1/8, 1/4T, 1/4, 1/2T, 1/4., 1/2, 1/2., 1 bar, 2 bars */
    void setDivision(size_t div) {
        division_ = div < 9 ? div : 8;
    }

    /** TEMPO's bipolar main knob, minus the division: 0..1, 0.5 = no random events.
     *  Below 0.5: random retrigger / reverse / pitch events, above: octave-up shimmer events
     *  with random pan. The distance from 0.5 is the probability. */
    void setRandom(float val) {
        shimmer_ = val > .5f;
        alt_control_ = fabsf(val - .5f) * 2.f;
    }

    /** An 8th note: the next read may start a random event */
    void setClockEdge() {
        clock_edge_ = true;
    }

    delayVoice myVoices[2];

    private:
    float *buffer_;
    uint32_t write_head_;
    uint32_t buffer_size_;
    float delay_samples_, delay_samples_target_;
    float cur_sig_l_, cur_sig_r_;
    float delay_feedback_amt_;

    int tempo_;
    size_t interval_us_;
    size_t division_;
    bool shimmer_;
    float alt_control_;
    size_t delay_div_position_;

    bool clock_edge_; // set by the clock pulses, in the audio callback too
    uint32_t since_edge_; // samples since the last edge
    chompi::Rng rng_;     // its random events, not newlib's shared rand()
    delayEvent event_type_[2];
    uint8_t curIdx, nextIdx;
};
