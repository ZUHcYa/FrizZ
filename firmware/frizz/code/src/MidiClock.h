/** @file MidiClock.h
 *  @brief MIDI clock input over TRS (UART) and USB, used to quantize looper recordings.
 *
 *  Only TimingClock ticks (24 PPQN) are used; every other message is drained and dropped.
 *  The first source that ticks gets locked, and ticks from the other source are ignored
 *  until the locked one has been silent for kClockTimeoutSamples, after which whichever
 *  source ticks next takes over.
 *
 *  Time is measured in samples on the audio clock (the running sample count passed into
 *  Process()), so tick timestamps line up with the audio the looper records. They're only as
 *  precise as the audio block (24 samples = 0.5ms) plus the transport's own latency.
 *
 *  The UART/USB setup is from WAVE's MidiManager.h. See LOOPER.md 1.4.
 */
#pragma once
#include "daisy.h"

using namespace daisy;

namespace chompi
{

static const uint32_t kTicksPerBeat = 24;
static const uint32_t kTicksPerBar = kTicksPerBeat * 4; // 4/4 fixed
static const uint32_t kClockTimeoutSamples = 24000;     // 0.5s at 48kHz

class MidiClock
{
public:
    enum class Source
    {
        NONE,
        TRS,
        USB,
    };

    MidiClock() {}
    ~MidiClock() {}

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;

        MidiUartHandler::Config uart_midi_cfg;
        uart_midi.Init(uart_midi_cfg);
        uart_midi.StartReceive();

        MidiUsbHandler::Config usb_midi_cfg;
        usb_midi_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
        usb_midi.Init(usb_midi_cfg);
        usb_midi.Listen();

        source_ = Source::NONE;
        ticks_ = 0;
        period_ = 0.f;
    }

    /** Drains both MIDI inputs. Call once per audio block from the audio callback.
     *  @param now running sample count at the start of this block */
    void Process(uint32_t now)
    {
        if (source_ != Source::NONE && now - last_tick_ > kClockTimeoutSamples)
            source_ = Source::NONE;

        // UART disables itself on errors such as overrun; Listen() restarts it
        uart_midi.Listen();

        while (uart_midi.HasEvents())
            HandleEvent(uart_midi.PopEvent(), Source::TRS, now);

        while (usb_midi.HasEvents())
            HandleEvent(usb_midi.PopEvent(), Source::USB, now);
    }

    /** True while a source is locked, i.e. a tick arrived within the timeout */
    inline bool HasClock() const { return source_ != Source::NONE; }
    inline Source GetSource() const { return source_; }

    /** Running count of ticks from the locked source. Only differences are meaningful:
     *  the looper snapshots it at the record press and counts from there. */
    inline uint32_t GetTicks() const { return ticks_; }

    /** Sample count of the most recent tick */
    inline uint32_t GetLastTickTime() const { return last_tick_; }

    /** Smoothed tick period in samples, 0 until two ticks have arrived from one source */
    inline float GetTickPeriod() const { return period_; }

    /** Smoothed tempo, 0 when there's no period yet */
    inline float GetBpm() const
    {
        return period_ > 0.f ? sample_rate_ * 60.f / (period_ * kTicksPerBeat) : 0.f;
    }

private:
    void HandleEvent(const MidiEvent& event, Source from, uint32_t now)
    {
        if (event.type != SystemRealTime || event.srt_type != TimingClock)
            return;

        if (source_ == Source::NONE)
        {
            // new lock: the first tick only gives a timestamp, no period yet
            source_ = from;
            period_ = 0.f;
        }
        else if (from != source_)
        {
            return;
        }
        else
        {
            const float interval = static_cast<float>(now - last_tick_);
            // smooth out block-granularity and USB-frame jitter
            period_ = period_ > 0.f ? period_ + .1f * (interval - period_) : interval;
        }

        ticks_++;
        last_tick_ = now;
    }

    MidiUartHandler uart_midi;
    MidiUsbHandler usb_midi;

    float sample_rate_;
    Source source_;
    uint32_t ticks_;
    uint32_t last_tick_;
    float period_;
};

} // namespace chompi
