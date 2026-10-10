// inserts.cpp: checks the two effects that had none of their own, the folder (FxFolder.h) and
// the slicer (FxSlicer.h): the folder's bypass when off and its level match, the slicer's
// patterns on the clock's 16ths, its chance and its stereo; and the shifter's mix (FxShifter.h,
// page 2). Exits 0 when everything passes.
// Run by unit.sh inserts.
#include <cmath>
#include <cstdio>
#include "check.h"
#include "FxChain.h"

using namespace chompi;

static const float kSr = 48000.f;

// ======== the folder ========

/** The folder at a drive, on a sine of amp: the output's level over the input's in dB, over
 *  the last half of a second */
static float FolderOver(float drive, float amp)
{
    Folder f;
    f.Init(kSr);
    f.SetParam(Folder::DRIVE, drive);
    f.SnapParams();
    f.SetOn(true);
    double in_sum = 0., out_sum = 0.;
    for (size_t i = 0; i < 48000; i++)
    {
        float l = amp * sinf(2.f * float(M_PI) * 220.f * i / kSr), r = l;
        const float in = l;
        f.Process(&l, &r);
        if (i >= 24000)
        {
            in_sum += 2. * in * in;
            out_sum += l * l + r * r;
        }
    }
    return static_cast<float>(10. * log10(out_sum / in_sum));
}

static void TestFolder()
{
    // off: what goes in comes out, bit for bit (its key's fade at 0)
    Folder f;
    f.Init(kSr);
    f.SetParam(Folder::DRIVE, 1.f);
    f.SnapParams();
    bool same = true;
    for (size_t i = 0; i < 48000; i++)
    {
        const float in = .5f * sinf(.01f * i);
        float l = in, r = -in;
        f.Process(&l, &r);
        same = same && l == in && r == -in;
    }
    Check(same, "folder: off, the input passes untouched, bit for bit");

    const float quiet = FolderOver(1.f, .03f), loud = FolderOver(1.f, .5f), low = FolderOver(0.f, .5f);
    printf("  folder: %+.1fdB at -30dBFS, %+.1fdB at -6dBFS (full drive), %+.1fdB at no drive\n", quiet,
           loud, low);
    Check(fabsf(quiet) < 3.f && fabsf(low) < 3.f, "folder: on, level-matched to its input within 3dB");
    // a loud input at full drive with the tone at its darkest (the effect's Init, not the
    // panel's open default) folds most of it into harmonics the tone takes out: the match
    // turns up by at most kMaxMatch (6dB), so it comes out up to 4dB quieter, never louder;
    // with the tone half open or more it matches to 0.1dB
    Check(loud < 1.f && loud > -6.5f, "folder: full drive on a loud input: no louder, at most 6dB down");
}

// ======== the slicer ========

/** The slicer on a steady input of 1 for bars at 120 BPM (a pulse every 2000 samples): the
 *  envelope hits on each channel, counted by its rising past .5 */
static void SlicerHits(float pattern, float chance, float stereo, int bars, int* hits_l, int* hits_r)
{
    Slicer s;
    s.Init(kSr);
    s.SetParam(Slicer::PATTERN, pattern);
    s.SetParam(Slicer::DECAY, 0.f); // the shortest: every hit stands alone
    s.SetParam(Slicer::CHANCE, chance);
    s.SetParam(Slicer::STEREO, stereo);
    s.SetOn(true);
    const uint32_t kSamplesPerPulse = 2000;
    float last[2] = {0.f, 0.f};
    *hits_l = *hits_r = 0;
    uint32_t pos = 0;
    // a 16th first, for the key's own press to fall away
    const uint32_t samples = (bars * 16 + 1) * kPulsesPer16th * kSamplesPerPulse;
    for (uint32_t i = 0; i < samples; i++)
    {
        if (i % kSamplesPerPulse == 0)
            s.ClockPulse(pos++);
        float l = 1.f, r = 1.f;
        s.Process(&l, &r);
        if (i >= kPulsesPer16th * kSamplesPerPulse)
        {
            *hits_l += l > .5f && last[0] <= .5f;
            *hits_r += r > .5f && last[1] <= .5f;
        }
        last[0] = l;
        last[1] = r;
    }
}

static void TestSlicer()
{
    const size_t n = Slicer::kNumPatterns;
    bool all = true;
    for (size_t p = 0; p < n; p++)
    {
        int l, r;
        SlicerHits((p + .5f) / n, 0.f, 0.f, 2, &l, &r);
        int want = 0;
        for (int b = 0; b < 8; b++)
            want += (kSlicerPatterns[p] >> b) & 1;
        want *= 4; // 8 steps are half a bar: 2 bars play the pattern 4 times
        if (l != want || r != want)
        {
            printf("  pattern %zu: %d / %d hits, want %d\n", p, l, r, want);
            all = false;
        }
    }
    Check(all, "slicer: every pattern hits on its steps of the clock's 16ths, both channels alike");

    int l, r, cl, cr;
    SlicerHits(1.f, 0.f, 0.f, 4, &l, &r);
    SlicerHits(1.f, 1.f, 0.f, 4, &cl, &cr);
    printf("  slicer: every 16th: %d hits in 4 bars, with chance at full %d\n", l, cl);
    Check(l == 64 && cl < 32 && cl > 0 && cl == cr,
          "slicer: chance flips steps at random, on both channels alike");

    int sl, sr;
    SlicerHits(.5f / n, 0.f, (2.5f) / n, 2, &sl, &sr);
    printf("  slicer: the sparsest pattern with stereo 2: %d hits left, %d right\n", sl, sr);
    Check(sl != sr, "slicer: stereo plays different patterns left and right");
}

// ======== the shifter's mix (page 2, #35) ========

static void TestShifterMix()
{
    // three shifters a fifth up on the same input, the mix at 1, .5 and 0: what each adds to
    // the input is in that proportion, since the mix only scales where the key's fade ends
    static Shifter sh[3];
    const float mixes[3] = {1.f, .5f, 0.f};
    for (size_t k = 0; k < 3; k++)
    {
        sh[k].Init(kSr);
        sh[k].SetParam(Shifter::SHIFT, .5f + 7.f / 24.f);
        sh[k].SetParam(Shifter::MIX, mixes[k]);
        sh[k].SnapParams();
        sh[k].SetOn(true);
    }
    float worst_half = 0.f, worst_dry = 0.f, wet = 0.f;
    for (size_t i = 0; i < 24000; i++)
    {
        const float in = .5f * sinf(2.f * float(M_PI) * 220.f * i / kSr);
        float l[3], r[3];
        for (size_t k = 0; k < 3; k++)
        {
            l[k] = r[k] = in;
            sh[k].Process(&l[k], &r[k]);
        }
        if (i < 4800)
            continue;
        wet = fmaxf(wet, fabsf(l[0] - in));
        worst_half = fmaxf(worst_half, fabsf((l[1] - in) - .5f * (l[0] - in)));
        worst_dry = fmaxf(worst_dry, fabsf(l[2] - in));
    }
    printf("  shifter mix: full adds up to %.3f, half off by %.2g, none by %.2g\n", wet, worst_half,
           worst_dry);
    Check(wet > .1f && worst_half < 1e-5f, "shifter mix: at .5, half of what fully shifted adds");
    Check(worst_dry == 0.f, "shifter mix: at 0, the input passes untouched, bit for bit");
}

int main()
{
    TestShifterMix();
    TestFolder();
    TestSlicer();
    return Finish();
}
