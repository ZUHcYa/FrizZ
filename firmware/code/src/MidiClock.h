/** @file MidiClock.h
 *  @brief MIDI clock input over TRS (UART) and USB, used to quantize looper recordings.
 *
 *  Only TimingClock ticks (24 PPQN) are used here; every other message goes to the listener,
 *  MidiControl.h, which plays the panel from it.
 *  The first source that ticks gets locked, and ticks from the other source are ignored
 *  until the locked one has been silent for kClockTimeoutSamples, after which whichever
 *  source ticks next takes over.
 *
 *  Time is measured in samples on the audio clock (the running sample count passed into
 *  Process()), so tick timestamps line up with the audio the looper records. They're only as
 *  precise as the audio block (24 samples = 0.5ms) plus the transport's own latency.
 *
 *  The UART/USB setup is from WAVE's MidiManager.h. See LOOPER.md 1.4.
 *
 *  The tempo factor (the settings page, SettingsPage.h) makes FRIZZ follow the clock at half
 *  or double its tempo: what the looper and TempoClock read (the ticks, their period, the
 *  tempo) counts every tick twice, or every other one. The event log keeps what was sent
 *  (SenderBpm, SenderLocks), since a bug report's card carries the factor.
 *
 *  One other message is read: the SysEx F0 7D 43 48 10 F7 asks for a restart, so a computer
 *  can get the CHOMPI back to the multi-firmware launcher without the power switch
 *  (firmware/flash.py). The header is the launcher's (7D, non-commercial, then "CH";
 *  github.com/sfaber02/CHOMPI, firmware/chompi-launcher/PROTOCOL.md); 10 is FRIZZ's own
 *  command, above the launcher's 01-04. MainLoop does the restart (chompi_main.cpp).
 */
#pragma once
#include <string.h>
#include "daisy.h"

using namespace daisy;

namespace chompi
{

// a restart, after the F0: the launcher's header, FRIZZ's command
static const uint8_t kRestartSysEx[] = {0x7D, 0x43, 0x48, 0x10};

static const uint32_t kTicksPerBeat = 24;
static const uint32_t kBeatsPerBar = 4;                 // 4/4 fixed
static const uint32_t kTicksPerBar = kTicksPerBeat * kBeatsPerBar;
static const uint32_t kClockTimeoutSamples = 24000;     // 0.5s at 48kHz

// How FRIZZ follows the clock: at half its tempo, as sent, or at double
enum class ClockFactor : uint8_t
{
    HALF,
    ONE,
    DOUBLE,
};

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
        locks_ = 0;
        changes_ = 0;
        counted_at_ = 0;
        factor_ = ClockFactor::ONE;
        period_ = 0.f;
        held_ = 0.f;
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

    /** How many times a source has locked, or the factor changed: a change means the clock
     *  was lost and found again, maybe within one block, or counts differently now, so a
     *  count of ticks across it means nothing */
    inline uint32_t GetLocks() const { return locks_ + changes_; }

    /** Running count of ticks from the locked source, by the factor. Only differences are
     *  meaningful: the looper snapshots it at the record press and counts from there. */
    inline uint32_t GetTicks() const { return ticks_; }

    /** Sample count of the most recent tick counted */
    inline uint32_t GetLastTickTime() const { return counted_at_; }

    /** Smoothed tick period in samples, by the factor, 0 until two ticks have arrived from one
     *  source */
    inline float GetTickPeriod() const
    {
        return factor_ == ClockFactor::HALF     ? 2.f * period_
               : factor_ == ClockFactor::DOUBLE ? .5f * period_
                                                : period_;
    }

    /** Smoothed tempo, by the factor, 0 when there's no period yet */
    inline float GetBpm() const { return Bpm(GetTickPeriod()); }

    /** The tempo and the locks as sent, whatever the factor: for the event log */
    inline float SenderBpm() const { return Bpm(period_); }
    inline uint32_t SenderLocks() const { return locks_; }

    /** From MainLoop: the factor, from the next tick on */
    void SetFactor(ClockFactor factor)
    {
        if (factor == factor_)
            return;
        ScopedIrqBlocker irq;
        factor_ = factor;
        changes_++;
    }
    inline ClockFactor Factor() const { return factor_; }

    /** True once a restart was asked for over MIDI, from either input */
    inline bool RestartRequested() const { return restart_; }

    /** Who gets every message but the clock, as it's read: in the audio callback */
    typedef void (*Listener)(void* context, const MidiEvent& event, bool usb);
    void SetListener(Listener listener, void* context)
    {
        listener_ = listener;
        listener_context_ = context;
    }

    /** A message out over USB. From MainLoop only: the transport waits for the bus */
    void SendUsb(uint8_t* bytes, size_t size) { usb_midi.SendMessage(bytes, size); }

private:
    inline float Bpm(float period) const
    {
        return period > 0.f ? sample_rate_ * 60.f / (period * kTicksPerBeat) : 0.f;
    }

    /** One step of the period's smoothing, against block-granularity and USB-frame jitter.
     *  A pair of ticks sent close together (in one USB packet, or a late one catching up)
     *  comes in as a long interval and a short one: smoothed one by one, they pulled the
     *  tempo a tenth up and down, and it was read mostly after the short one, 5% fast. So a
     *  long interval waits for the next tick (HandleEvent) */
    inline void Smooth(float interval)
    {
        period_ = period_ > 0.f ? period_ + .1f * (interval - period_) : interval;
    }

    void HandleEvent(const MidiEvent& event, Source from, uint32_t now)
    {
        if (event.type == SystemCommon && event.sc_type == SystemExclusive)
        {
            restart_ = restart_
                       || (event.sysex_message_len == sizeof(kRestartSysEx)
                           && memcmp(event.sysex_data, kRestartSysEx, sizeof(kRestartSysEx)) == 0);
        }
        if (event.type != SystemRealTime || event.srt_type != TimingClock)
        {
            if (listener_)
                listener_(listener_context_, event, from == Source::USB);
            return;
        }

        if (source_ == Source::NONE)
        {
            // new lock: the first tick only gives a timestamp, no period yet
            source_ = from;
            period_ = 0.f;
            locks_++;
            held_ = 0.f;
            skipped_ = true; // at half, the first tick counts
        }
        else if (from != source_)
        {
            return;
        }
        else
        {
            const float interval = static_cast<float>(now - last_tick_);
            if (held_ > 0.f)
            {
                // a long interval waited for this one: far shorter, they were a pair (two
                // ticks sent together, or a late one catching up), so their mean is the
                // period; otherwise the long one was real, a tempo change or a lost tick
                if (interval < .5f * period_)
                    Smooth(.5f * (held_ + interval));
                else
                {
                    Smooth(held_);
                    Smooth(interval);
                }
                held_ = 0.f;
            }
            else if (period_ > 0.f && interval > 1.5f * period_)
                held_ = interval; // waits for the next tick (above)
            else
                Smooth(interval);
        }

        last_tick_ = now;
        // counted twice, as it came, or every other one
        if (factor_ == ClockFactor::HALF)
        {
            skipped_ = !skipped_;
            if (skipped_)
                return;
        }
        ticks_ += factor_ == ClockFactor::DOUBLE ? 2 : 1;
        counted_at_ = now;
    }

    MidiUartHandler uart_midi;
    MidiUsbHandler usb_midi;

    float sample_rate_;
    Source source_;
    uint32_t ticks_;      // counted, by the factor
    uint32_t locks_;
    uint32_t changes_;    // of the factor
    uint32_t last_tick_;  // the last tick that came
    uint32_t counted_at_; // the last tick counted
    volatile ClockFactor factor_;
    bool skipped_ = false; // at half: the last tick wasn't counted
    float period_;
    float held_ = 0.f; // a long interval waiting for the next tick, 0 for none
    volatile bool restart_ = false;
    Listener listener_ = nullptr;
    void* listener_context_ = nullptr;
};

} // namespace chompi
