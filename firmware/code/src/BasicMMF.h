#pragma once

namespace chompi
{

/** A cheap state-variable-style filter, lowpass or highpass */
class BasicMMF
{
  public:
    enum class Mode
    {
        Lowpass,
        Highpass,
    };
    BasicMMF()
    : mode_(Mode::Lowpass),
      freq_(0.5f),
      res_(0.5f),
      buf0_(0.f),
      buf1_(0.f),
      fb_amt_(0.f)
    {
    }

    ~BasicMMF() {}

    void Init(float) { CalculateFeedback(); }

    float Process(const float in)
    {
        buf0_ += freq_ * (in - buf0_ + fb_amt_ * (buf0_ - buf1_));
        buf1_ += freq_ * (buf0_ - buf1_);
        switch(mode_)
        {
            case Mode::Lowpass: return buf1_;
            case Mode::Highpass: return in - buf0_;
            default: return 0.f;
        }
    }

    /** Set freq as 0-1 input where 1 is the nyquist frequency
     * @todo make this take actual freq... */
    inline void SetFreq(float f)
    {
        freq_ = f;
        CalculateFeedback();
    }

    inline void SetRes(float r)
    {
        res_ = r;
        CalculateFeedback();
    }

    inline void SetMode(Mode m) { mode_ = m; }

    /** The other channel's frequency and resonance, without working them out again */
    inline void CopySettings(const BasicMMF& other)
    {
        freq_ = other.freq_;
        res_ = other.res_;
        fb_amt_ = other.fb_amt_;
    }

  private:
    Mode  mode_;
    float freq_, res_;

    float buf0_, buf1_;
    float fb_amt_;

    inline void CalculateFeedback() { fb_amt_ = res_ + (res_ / (1.f - freq_)); }
};

} //namespace chompi
