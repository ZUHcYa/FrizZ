/** @file FxFolder.h
 *  @brief The folder: a wavefolder, sine to triangle, antialiased, level-matched.
 */
#pragma once
#include "FxCommon.h"

namespace chompi
{

/** A wavefolder in the Buchla / Serge manner: the signal is driven past the fold points and
 *  mirrored back, each fold adding harmonics. The fold is a sine (smooth, the classic
 *  Buchla shape) crossfaded into a triangle (sharp, brighter), with the symmetry adding a
 *  bias so the folds go uneven and bring in even harmonics.
 *
 *  DaisySP's Wavefolder is the triangle alone, unfiltered; here the fold is antialiased
 *  with first-order ADAA (the fold's antiderivative, differenced), DC-blocked, and followed
 *  by a lowpass. The sine fold barely aliases anyway; on the triangle the ADAA takes off
 *  5-13dB, which at high drive on bright material still leaves some grit. A folder's output is about full scale whatever goes in, so the result is
 *  matched to the input's level (linked stereo, ~50ms): punching in changes the sound, not
 *  the loudness. Fully wet while on.
 *  Params: 0 drive, 1 shape (sine to triangle), 2 tone, 3 symmetry. */
class Folder : public FxBase
{
public:
    enum Param
    {
        DRIVE,
        SHAPE,
        TONE,
        SYMMETRY,
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        env_coeff_ = 1.f - expf(-1.f / (.05f * sample_rate));
        for (size_t c = 0; c < 2; c++)
        {
            u1_[c] = 0.f;
            f1_[c] = Antiderivative(0.f, 0.f);
            lp_[c] = 0.f;
            dc_[c].Init(sample_rate);
        }
        env_in_ = env_out_ = 0.f;
        gate_.Init();

        for (size_t i = 0; i < kNumFxParams; i++)
            SetParam(i, 0.f);
        drive_.Snap();
        shape_.Snap();
        bias_.Snap();
        tone_coeff_.Snap();
    }

    void Process(float* l, float* r)
    {
        const float gate = gate_.Process();
        const float drive = drive_.Process();
        const float shape = shape_.Process();
        const float bias = bias_.Process();
        const float tone_coeff = tone_coeff_.Process();

        float* const io[2] = {l, r};
        float wet[2];
        for (size_t c = 0; c < 2; c++)
        {
            const float u = *io[c] * drive + bias;
            const float f = Antiderivative(u, shape);
            // ADAA: the fold averaged over the step from the last sample, which takes some
            // of the aliasing out of the triangle's corners; the plain fold at the midpoint
            // when the step is too small to divide by
            const float du = u - u1_[c];
            const float y = fabsf(du) > kAdaaMinStep ? (f - f1_[c]) / du
                                                     : Fold(.5f * (u + u1_[c]), shape);
            u1_[c] = u;
            f1_[c] = f;

            // the bias's DC out, then the tone
            lp_[c] += tone_coeff * (dc_[c].Process(y) - lp_[c]);
            wet[c] = lp_[c];
        }

        // level match: the output scaled to the input's level
        fonepole(env_in_, *l * *l + *r * *r, env_coeff_);
        fonepole(env_out_, wet[0] * wet[0] + wet[1] * wet[1], env_coeff_);
        const float match = fminf(sqrtf((env_in_ + kEnvFloor) / (env_out_ + kEnvFloor)), kMaxMatch);

        *l += gate * (wet[0] * match - *l);
        *r += gate * (wet[1] * match - *r);
    }

    void SetParam(size_t param, float val) override
    {
        switch (param)
        {
        case DRIVE:
            // 1x (the sine slightly saturating, the triangle clean) to 32x, many folds
            drive_.target = powf(kMaxDrive, val);
            break;
        case SHAPE:
            shape_.target = val;
            break;
        case SYMMETRY:
            // up to a quarter of the fold's period: the sine becomes a cosine, all even
            bias_.target = val;
            break;
        case TONE:
            // lowpass from 200Hz to 20kHz, fully open at the top, as the crusher's
            tone_coeff_.target = ToneCoeff(val, sample_rate_);
            break;
        default:
            break;
        }
    }

private:
    static constexpr float kMaxDrive = 32.f;
    static constexpr float kAdaaMinStep = 1e-3f;
    static constexpr float kEnvFloor = 1e-7f; // -70dB: silence stays at about unity
    static constexpr float kMaxMatch = 2.f;   // the most the level match turns up

    /** The fold: period 4, 0 at 0, 1 at 1, back through 0 at 2 to -1 at 3, so below 1 it's
     *  close to the input, past it mirrored back. A sine crossfaded into a triangle. */
    static inline float Fold(float u, float shape)
    {
        const float sine = sinf(HALFPI_F * u);
        const float v = Wrap(u);
        const float tri = v < 2.f ? v - 1.f : 3.f - v;
        return sine + shape * (tri - sine);
    }

    /** Fold's antiderivative, for the ADAA */
    static inline float Antiderivative(float u, float shape)
    {
        const float sine = -(2.f / PI_F) * cosf(HALFPI_F * u);
        const float v = Wrap(u);
        const float tri = v < 2.f ? .5f * v * v - v : 3.f * v - .5f * v * v - 4.f;
        return sine + shape * (tri - sine);
    }

    /** u + 1 wrapped into 0..4, where the triangle starts its period at -1 */
    static inline float Wrap(float u)
    {
        const float v = u + 1.f;
        return v - 4.f * floorf(.25f * v);
    }

    float sample_rate_;
    float env_coeff_;
    float u1_[2], f1_[2]; // the last sample's fold input and antiderivative
    float lp_[2];
    daisysp::DcBlock dc_[2];
    float env_in_, env_out_; // mean squares, linked stereo
    Smoothed drive_, shape_, bias_, tone_coeff_;
};

} // namespace chompi
