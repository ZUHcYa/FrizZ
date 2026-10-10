// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------

#pragma once
#include "daisysp.h"

#define SLOPE(out, in, positive, negative)                \
    {                                                     \
        float error = (in)-out;                           \
        out += (error > 0 ? positive : negative) * error; \
    }

namespace chompi
{
/** Simple Peak Limiter, as the safety limiter on each output (ProcessComp, passthroughEngine.h)

This was extracted from pichenettes/stmlib.

Credit to pichenettes/Mutable Instruments
*/
class Limiter
{
  public:
    Limiter() {}
    ~Limiter() {}
    
    /** Initializes the Limiter instance. 
    */
    void Init()
    {
        peak_ = 0.5f;
        gain_ = 0.f; // rises to the gain within ms: the outputs fade in at boot
    }

    /** Compression like setup */
    float ProcessComp(float in, float pregain, float thresh, float ratio, float makeup)
    {
        const float pre = in * pregain;
        const float peak = fabsf(pre);
        SLOPE(peak_, peak, 0.05f, 0.0002f);
        const float gain = (peak_ <= thresh ? 1.f : 1.f / (ratio * (1.f + (peak_ - thresh))) );
        SLOPE(gain_, gain, .001f, .005f);
        return daisysp::SoftClip(pre * gain_ * makeup);
    }

    /** The gain it applies now, 0..1 (1: not limiting) */
    inline float Gain() const { return gain_; }


  private:
    float peak_;
    float gain_;
};
} // namespace chompi