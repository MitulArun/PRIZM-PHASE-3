/*
  ==============================================================================

    PRIZM V2 - stereo-linked feed-forward bus compressor.

  ==============================================================================
*/

#include "Compressor.h"

namespace prizm::dsp
{

namespace
{
    constexpr double rmsWindowSeconds = 0.005;

    double timeCoeff (double sampleRate, double ms) noexcept
    {
        return std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sampleRate));
    }
}

void Compressor::prepare (double sampleRate)
{
    fs = sampleRate;
    threshold.prepare (fs, 0.05);
    knee.prepare (fs, 0.05);
    makeup.prepare (fs, 0.05);
    mix.prepare (fs, 0.05);
    moduleMix.prepare (fs, 0.02);
    rmsCoeff = timeCoeff (fs, rmsWindowSeconds * 1000.0);
    currentHpfHz = -1.0f;
    reset (target);
}

void Compressor::setSettings (const CompressorSettings& s) noexcept
{
    const bool timesChanged = s.attackMs != target.attackMs || s.releaseMs != target.releaseMs;
    target = s;

    threshold.setTarget (s.thresholdDb);
    knee.setTarget (std::max (0.0f, s.kneeDb));
    makeup.setTarget (s.makeupDb);
    mix.setTarget (clampValue (s.mixPercent, 0.0f, 100.0f) * 0.01f);
    moduleMix.setTarget (s.enabled ? 1.0f : 0.0f);
    ratio = std::max (1.0f, s.ratio);

    if (timesChanged || attackCoeff == 0.0)
        updateTimeConstants();

    if (s.detectorHpfHz != currentHpfHz)
    {
        currentHpfHz = s.detectorHpfHz;
        detectorHpf.setCoeffs (BiquadCoeffs::highPass (fs, std::max (20.0f, currentHpfHz), 0.7071));
    }
}

void Compressor::reset (const CompressorSettings& s) noexcept
{
    setSettings (s);
    threshold.reset (threshold.getTarget());
    knee.reset (knee.getTarget());
    makeup.reset (makeup.getTarget());
    mix.reset (mix.getTarget());
    moduleMix.reset (moduleMix.getTarget());
    updateTimeConstants();
    rmsState = peakState = grState = 0.0;
    detectorHpf.reset();
    lastBlockGr = 0.0f;
}

void Compressor::updateTimeConstants() noexcept
{
    attackCoeff = timeCoeff (fs, target.attackMs);
    releaseCoeff = timeCoeff (fs, target.releaseMs);
}

float Compressor::gainComputerDb (float x, float t, float r, float w) noexcept
{
    const float over = x - t;

    if (w > 0.0f && 2.0f * std::abs (over) <= w)
    {
        const float k = over + w * 0.5f;
        return x + (1.0f / r - 1.0f) * k * k / (2.0f * w);
    }

    if (over <= 0.0f)
        return x;

    return t + over / r;
}

void Compressor::process (float* left, float* right, int numSamples) noexcept
{
    lastBlockGr = 0.0f;

    if (! moduleMix.isSmoothing() && moduleMix.getCurrent() <= 0.0f)
    {
        // Bypassed and settled: keep detector idle so it re-engages cleanly.
        rmsState = peakState = grState = 0.0;
        detectorHpf.reset();
        return;
    }

    const bool useHpf = currentHpfHz > 20.5f;
    float maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float thr = threshold.next();
        const float kn = knee.next();
        const float mk = makeup.next();
        const float mx = mix.next();
        const float mod = moduleMix.next();

        const float dryL = left[i], dryR = right[i];

        float scL = dryL, scR = dryR;
        if (useHpf)
        {
            scL = detectorHpf.process (scL, 0);
            scR = detectorHpf.process (scR, 1);
        }

        // Linked RMS-style detector: loudest channel drives both.
        const double power = std::max ((double) scL * scL, (double) scR * scR);
        rmsState = power + rmsCoeff * (rmsState - power);

        const float levelDb = (float) (10.0 * std::log10 (rmsState + 1.0e-20));
        const float targetGr = levelDb - gainComputerDb (levelDb, thr, ratio, kn);

        // Smooth decoupled peak detector (release stage, then attack stage).
        peakState = std::max ((double) targetGr, releaseCoeff * peakState + (1.0 - releaseCoeff) * targetGr);
        grState = attackCoeff * grState + (1.0 - attackCoeff) * peakState;

        const float gr = (float) grState;
        maxGr = std::max (maxGr, gr);

        const float gain = dbToGain (mk - gr);
        const float wetMix = mx * mod;

        left[i]  = dryL + (dryL * gain - dryL) * wetMix;
        right[i] = dryR + (dryR * gain - dryR) * wetMix;
    }

    lastBlockGr = maxGr;
}

} // namespace prizm::dsp
