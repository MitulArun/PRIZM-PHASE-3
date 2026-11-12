/*
  ==============================================================================

    PRIZM V2 - mid/side stereo imager.

  ==============================================================================
*/

#include "StereoImager.h"

namespace prizm::dsp
{

void StereoImager::prepare (double sampleRate)
{
    fs = sampleRate;
    width.prepare (fs, 0.05);
    moduleMix.prepare (fs, 0.02);
    monoMix.prepare (fs, 0.02);
    monoFreq.prepare (fs, 0.05);
    currentFilterHz = -1.0f;
    reset (target);
}

void StereoImager::setSettings (const ImagerSettings& s) noexcept
{
    target = s;
    width.setTarget (clampValue (s.widthPercent, 0.0f, 200.0f) * 0.01f);
    moduleMix.setTarget (s.enabled ? 1.0f : 0.0f);
    monoMix.setTarget (s.monoBassOn ? 1.0f : 0.0f);
    monoFreq.setTarget (clampValue (s.monoBassHz, 20.0f, 1000.0f));
}

void StereoImager::reset (const ImagerSettings& s) noexcept
{
    setSettings (s);
    width.reset (width.getTarget());
    moduleMix.reset (moduleMix.getTarget());
    monoMix.reset (monoMix.getTarget());
    monoFreq.reset (monoFreq.getTarget());
    sideHpf[0].reset();
    sideHpf[1].reset();
    updateFilter (monoFreq.getCurrent());
}

void StereoImager::updateFilter (float hz) noexcept
{
    currentFilterHz = hz;
    const auto c = BiquadCoeffs::highPass (fs, hz, 0.70710678);
    sideHpf[0].setCoeffs (c);
    sideHpf[1].setCoeffs (c);
}

void StereoImager::process (float* left, float* right, int numSamples) noexcept
{
    if (! moduleMix.isSmoothing() && moduleMix.getCurrent() <= 0.0f)
    {
        sideHpf[0].reset();
        sideHpf[1].reset();
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float w = width.next();
        const float mod = moduleMix.next();
        const float mono = monoMix.next();
        const float freq = monoFreq.next();

        if ((i & 15) == 0 && freq != currentFilterHz)
            updateFilter (freq);

        const float l = left[i], r = right[i];
        const float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r) * w;

        if (mono > 0.0f || monoMix.isSmoothing())
        {
            const float hp = sideHpf[1].process (sideHpf[0].process (side, 0), 0);
            side += (hp - side) * mono;
        }
        else
        {
            sideHpf[0].reset();
            sideHpf[1].reset();
        }

        const float outL = mid + side, outR = mid - side;
        left[i]  = l + (outL - l) * mod;
        right[i] = r + (outR - r) * mod;
    }
}

} // namespace prizm::dsp
