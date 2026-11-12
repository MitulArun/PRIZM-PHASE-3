/*
  ==============================================================================

    PRIZM V2 - harmonic saturation (4x oversampled).

  ==============================================================================
*/

#include "Saturator.h"

namespace prizm::dsp
{

namespace
{
    constexpr float tubeBias = 0.2f;
    constexpr double dcBlockHz = 5.0;

    float driveToGain (float driveDb) noexcept
    {
        // 0 dB drive is almost clean at full scale; 24 dB drive is heavy.
        return 0.25f * dbToGain (driveDb);
    }
}

float Saturator::shape (int type, float x, float k) noexcept
{
    switch (type)
    {
        case 1:   // Tube: biased tanh, normalised to unity small-signal slope
        {
            static const float tb = std::tanh (tubeBias);
            static const float slope = 1.0f - tb * tb;
            return (std::tanh (k * x + tubeBias) - tb) / (k * slope);
        }

        case 2:   // Soft clip: cubic below the knee, flat above
        {
            const float u = k * x;
            if (u >= 1.0f)  return (2.0f / 3.0f) / k;
            if (u <= -1.0f) return (-2.0f / 3.0f) / k;
            return (u - u * u * u / 3.0f) / k;
        }

        default:  // Tape: symmetric tanh
            return std::tanh (k * x) / k;
    }
}

void Saturator::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    oversampler.prepare (maxBlockSize);

    for (int ch = 0; ch < 2; ++ch)
    {
        dryDelay[ch].prepare (oversampler.getLatencySamples());
        dryDelay[ch].setDelay (oversampler.getLatencySamples());
        wetBuffer[ch].assign ((size_t) maxBlockSize, 0.0f);
    }

    drive.prepare (fs, 0.05);
    mix.prepare (fs, 0.05);
    moduleMix.prepare (fs, 0.02);
    typeFade.prepare (fs, 0.005);
    dcCoeff = std::exp (-2.0 * pi * dcBlockHz / (fs * 4.0));

    reset (target);
}

void Saturator::setSettings (const SaturatorSettings& s) noexcept
{
    target = s;
    drive.setTarget (clampValue (s.driveDb, 0.0f, 24.0f));
    mix.setTarget (clampValue (s.mixPercent, 0.0f, 100.0f) * 0.01f);
    moduleMix.setTarget (s.enabled ? 1.0f : 0.0f);
    pendingType = clampValue (s.type, 0, 2);
}

void Saturator::reset (const SaturatorSettings& s) noexcept
{
    setSettings (s);
    drive.reset (drive.getTarget());
    mix.reset (mix.getTarget());
    moduleMix.reset (moduleMix.getTarget());
    currentType = pendingType;
    typeFade.reset (1.0f);
    oversampler.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        dryDelay[ch].clear();
        dcState[ch][0] = dcState[ch][1] = 0.0;
    }
}

void Saturator::process (float* left, float* right, int numSamples) noexcept
{
    float* io[2] = { left, right };

    // Per-sample control values shared by both channels.
    float driveVals[1024], mixVals[1024], modVals[1024];
    const int n = std::min (numSamples, 1024);

    for (int i = 0; i < n; ++i)
    {
        if (pendingType != currentType)
        {
            typeFade.setTarget (0.0f);
            if (! typeFade.isSmoothing() && typeFade.getCurrent() <= 0.0f)
            {
                currentType = pendingType;
                typeFade.setTarget (1.0f);
            }
        }

        driveVals[i] = driveToGain (drive.next());
        mixVals[i] = mix.next() * typeFade.next();
        modVals[i] = moduleMix.next();
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        float* up = oversampler.upsample (ch, io[ch], n);
        double x1 = dcState[ch][0], y1 = dcState[ch][1];

        for (int i = 0; i < n * 4; ++i)
        {
            const int b = i >> 2;
            const float x = up[i];
            const float s = shape (currentType, x, driveVals[b]);

            // DC blocker on the shaped signal only (tube curve is asymmetric).
            const double y = (double) s - x1 + dcCoeff * y1;
            x1 = s;
            y1 = y;

            up[i] = x + ((float) y - x) * mixVals[b];
        }

        dcState[ch][0] = x1;
        dcState[ch][1] = y1;

        oversampler.downsample (ch, up, wetBuffer[ch].data(), n);

        float* out = io[ch];
        const float* wet = wetBuffer[ch].data();
        for (int i = 0; i < n; ++i)
        {
            const float dry = dryDelay[ch].process (out[i]);
            out[i] = dry + (wet[i] - dry) * modVals[i];
        }
    }
}

} // namespace prizm::dsp
