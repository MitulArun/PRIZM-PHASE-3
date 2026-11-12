/*
  ==============================================================================

    PRIZM V2 - mid/side stereo imager.

    M = (L+R)/2, S = (L-R)/2. Width scales S (0% = mono, 100% = unchanged,
    200% = double side level). "Mono bass" high-passes the side signal with a
    24 dB/oct Linkwitz-Riley filter so everything below the crossover is
    collapsed to the centre - the standard fix for phase-smeared low end that
    disappears on mono club systems, phones and vinyl.

  ==============================================================================
*/

#pragma once

#include "Biquad.h"

namespace prizm::dsp
{

struct ImagerSettings
{
    bool enabled = false;
    float widthPercent = 100.0f;
    bool monoBassOn = false;
    float monoBassHz = 120.0f;
};

class StereoImager
{
public:
    void prepare (double sampleRate);
    void setSettings (const ImagerSettings& s) noexcept;
    void reset (const ImagerSettings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

private:
    void updateFilter (float hz) noexcept;

    double fs = 44100.0;
    ImagerSettings target;
    LinearSmoother width, moduleMix, monoMix, monoFreq;
    Biquad sideHpf[2];       // two cascaded 2nd-order Butterworth = LR4
    float currentFilterHz = -1.0f;
};

} // namespace prizm::dsp
