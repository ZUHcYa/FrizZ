// Delay check: the delay's voices (granularDelay.h) start where its events expect them.
// Built and run by unit.sh delay.
//  - right after Init, a pitch-up event reads from half a bar back, as it does after a
//    division change: the voices start on the delay's own division
//  - the random events, on 8th-note edges: none with the random knob in the middle or with
//    the key off (the tail's edges); towards 0 retriggers, reverses and pitch events, as often
//    as the knob says (half the edges at 0, a quarter at 0.25), each kind about as often; a
//    reverse that wouldn't fit the buffer (2 bars at 50 BPM) a retrigger instead; towards 1
//    only octave-up shimmers, panned at random to both sides
#include <algorithm>
#include <cmath>
#include <vector>
#include "granularDelay.h"
#include "check.h"

static const size_t kFrames = 480000; // FRIZZ's 10s buffer
static float buffer[kFrames * 2];

/** How far back a voice's read head starts, in samples, from the write head */
static float Behind(const delayVoice& v)
{
    float behind = static_cast<float>(v.write_head_) - v.read_head_;
    if (behind < 0.f)
        behind += static_cast<float>(kFrames);
    return behind;
}

static void TestPitchUp(bool turn_division)
{
    static granularDelay d;
    d.Init(buffer, kFrames);
    if (turn_division)
    {
        // over to 1/2 and back to 1/4, as a turn of the knob does
        d.setDivision(5);
        for (int i = 0; i < 2000; i++)
        {
            float l = 0.f, r = 0.f;
            d.write(0.f, 0.f);
            d.read(&l, &r);
        }
        d.setDivision(kStartDiv);
    }
    for (int i = 0; i < 2000; i++)
    {
        float l = 0.f, r = 0.f;
        d.write(0.f, 0.f);
        d.read(&l, &r);
    }
    // a bar at 120 BPM: 96000 samples; the event's voice starts half of it back
    delayVoice& v = d.myVoices[1];
    v.setNextEvent(delayVoice::PITCH_UP, 0.f);
    v.startFadeIn();
    const float behind = Behind(v);
    printf("      pitch-up %s: starts %.0f samples back\n",
           turn_division ? "after a division change" : "right after Init", behind);
    Check(fabsf(behind - 48000.f) < 2.f, turn_division
              ? "pitch-up after a division change: half a bar back"
              : "pitch-up right after Init: half a bar back too");
}

/** Runs `edges` 8th-note edges at a tempo, a division and the random knob, events allowed or
 *  not (the key on or off); counts the events that start, by kind (delayVoice::delayEvent),
 *  and keeps their pans */
static size_t Events(float random, bool events, float bpm, size_t div, size_t edges,
                     size_t kinds[4], std::vector<float>& pans)
{
    static granularDelay d;
    d.Init(buffer, kFrames);
    d.SetTempo(bpm);
    d.setDivision(div);
    d.setRandom(random);
    const size_t eighth = static_cast<size_t>(60.f * 48000.f / (bpm * 2.f));
    float l, r;
    for (size_t i = 0; i < kFrames; i++) // the buffer full, the division crossfaded in
    {
        d.write(.1f * sinf(.01f * i), 0.f);
        d.read(&l, &r);
    }
    size_t count = 0;
    for (size_t e = 0; e < edges; e++)
    {
        d.setClockEdge(events);
        for (size_t i = 0; i < eighth; i++)
        {
            d.write(.1f * sinf(.01f * i), 0.f);
            d.read(&l, &r);
            if (i > 0)
                continue;
            // an event starts: a voice fading in on it
            for (delayVoice& v : d.myVoices)
                if (v.fading_in_ && v.event_crossfade_counter_ == 1 && v.curEvent != delayVoice::NONE)
                {
                    count++;
                    kinds[v.curEvent]++;
                    pans.push_back(v.curPan);
                }
        }
    }
    return count;
}

static void TestEvents()
{
    size_t none[4] = {}, half[4] = {}, quarter[4] = {}, shimmer[4] = {}, tail[4] = {}, slow[4] = {};
    std::vector<float> pans, shimmer_pans;
    const size_t at_middle = Events(.5f, true, 120.f, kStartDiv, 400, none, pans);
    const size_t at_0 = Events(0.f, true, 120.f, kStartDiv, 400, half, pans);
    const size_t at_quarter = Events(.25f, true, 120.f, kStartDiv, 400, quarter, pans);
    const size_t off = Events(0.f, false, 120.f, kStartDiv, 400, tail, pans);
    const size_t at_1 = Events(1.f, true, 120.f, kStartDiv, 400, shimmer, shimmer_pans);
    printf("      events in 400 8ths: %zu in the middle, %zu at 0 (retrig %zu, reverse %zu, up %zu, "
           "down %zu), %zu at 0.25, %zu off, %zu at 1\n",
           at_middle, at_0, half[0], half[1], half[2], half[3], at_quarter, off, at_1);
    Check(at_middle == 0, "events: the random knob in the middle: none");
    Check(off == 0, "events: the key off, a tail's edges: none");
    Check(at_0 > 160 && at_0 < 240, "events: at 0, on about half the 8ths");
    bool each = true;
    for (size_t k = 0; k < 4; k++)
        each &= half[k] > at_0 / 6;
    Check(each, "events: retriggers, reverses, pitch-ups and -downs, each about as often");
    Check(at_quarter > 70 && at_quarter < 130, "events: at 0.25, on about a quarter");
    bool centred = true;
    for (float p : pans)
        centred &= p == 0.f;
    Check(centred, "events: towards 0 they stay in the centre");
    Check(at_1 > 160 && at_1 < 240 && shimmer[2] == at_1, "events: at 1, shimmers (octave up) on about half");
    float lo = 0.f, hi = 0.f;
    for (float p : shimmer_pans)
    {
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    printf("      shimmer pans from %.2f to %.2f\n", lo, hi);
    Check(lo < -.5f && hi > .5f && lo >= -1.f && hi <= 1.f, "events: panned at random to both sides");
    pans.clear();
    const size_t at_50 = Events(0.f, true, 50.f, 8, 200, slow, pans);
    printf("      2 bars at 50 BPM: %zu events, retrig %zu, reverse %zu\n", at_50, slow[0], slow[1]);
    Check(at_50 > 0 && slow[1] == 0 && slow[0] > at_50 / 3,
          "events: a reverse that wouldn't fit the buffer (2 bars at 50 BPM) is a retrigger");
}

int main()
{
    TestPitchUp(true);
    TestPitchUp(false);
    TestEvents();
    return Finish();
}
