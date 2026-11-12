/*
  ==============================================================================

    PRIZM V2 - lookahead true-peak brickwall limiter.

  ==============================================================================
*/

#include "Limiter.h"

namespace prizm::dsp
{

namespace
{
    constexpr double lookaheadSeconds = 0.002;
}

void Limiter::prepare (double sampleRate)
{
    fs = sampleRate;
    lookahead = std::max (8, (int) std::lround (fs * lookaheadSeconds));
    holdWindow = lookahead + 2;

    const int latency = getLatencySamples();
    for (int ch = 0; ch < 2; ++ch)
    {
        wetDelay[ch].prepare (latency);
        wetDelay[ch].setDelay (latency);
        dryDelay[ch].prepare (latency);
        dryDelay[ch].setDelay (latency);
    }

    dqValue.assign ((size_t) holdWindow + 2, 1.0f);
    dqIndex.assign ((size_t) holdWindow + 2, 0);
    boxRing.assign ((size_t) lookahead, 1.0f);

    inGain.prepare (fs, 0.05);
    ceiling.prepare (fs, 0.05);
    moduleMix.prepare (fs, 0.02);

    reset (target);
}

void Limiter::setSettings (const LimiterSettings& s) noexcept
{
    target = s;
    inGain.setTarget (dbToGain (clampValue (s.gainDb, -24.0f, 36.0f)));
    ceiling.setTarget (dbToGain (clampValue (s.ceilingDb, -24.0f, 0.0f)));
    moduleMix.setTarget (s.enabled ? 1.0f : 0.0f);
    releaseCoeff = std::exp (-1.0 / (std::max (1.0f, s.releaseMs) * 0.001 * fs));
}

void Limiter::reset (const LimiterSettings& s) noexcept
{
    setSettings (s);
    inGain.reset (inGain.getTarget());
    ceiling.reset (ceiling.getTarget());
    moduleMix.reset (moduleMix.getTarget());

    for (int ch = 0; ch < 2; ++ch)
    {
        detectors[ch].reset();
        wetDelay[ch].clear();
        dryDelay[ch].clear();
    }

    dqHead = dqSize = 0;
    sampleIndex = 0;
    std::fill (boxRing.begin(), boxRing.end(), 1.0f);
    boxPos = 0;
    boxSum = (double) lookahead;
    boxRecalcCounter = 0;
    releaseState = 1.0f;
    lastBlockGr = 0.0f;
}

void Limiter::pushHold (float value) noexcept
{
    const int cap = (int) dqValue.size();

    // Drop larger values from the back - they can never be the minimum again.
    while (dqSize > 0)
    {
        const int back = (dqHead + dqSize - 1) % cap;
        if (dqValue[(size_t) back] < value)
            break;
        --dqSize;
    }

    const int slot = (dqHead + dqSize) % cap;
    dqValue[(size_t) slot] = value;
    dqIndex[(size_t) slot] = sampleIndex;
    ++dqSize;

    // Expire entries that left the window.
    while (dqSize > 0 && dqIndex[(size_t) dqHead] <= sampleIndex - holdWindow)
    {
        dqHead = (dqHead + 1) % cap;
        --dqSize;
    }
}

void Limiter::process (float* left, float* right, int numSamples) noexcept
{
    float minGain = 1.0f;
    const bool tp = target.truePeak;
    const double invLookahead = 1.0 / lookahead;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGain.next();
        const float ceil = ceiling.next();
        const float mod = moduleMix.next();

        const float dryL = left[i], dryR = right[i];
        const float xL = dryL * g, xR = dryR * g;

        const float peak = tp ? std::max (detectors[0].process (xL), detectors[1].process (xR))
                              : std::max (detectors[0].processSamplePeak (xL), detectors[1].processSamplePeak (xR));

        const float required = peak > ceil ? ceil / peak : 1.0f;

        pushHold (required);
        const float held = dqValue[(size_t) dqHead];

        // Instant attack (the box filter shapes it), exponential release.
        releaseState = held < releaseState ? held
                                           : (float) (held + releaseCoeff * (releaseState - held));

        boxSum += (double) releaseState - boxRing[(size_t) boxPos];
        boxRing[(size_t) boxPos] = releaseState;
        if (++boxPos >= lookahead)
        {
            boxPos = 0;

            // Periodically recompute the running sum to cancel rounding drift.
            if (++boxRecalcCounter >= 64)
            {
                boxRecalcCounter = 0;
                double s = 0.0;
                for (float v : boxRing)
                    s += v;
                boxSum = s;
            }
        }

        const float gain = (float) std::min (1.0, boxSum * invLookahead);
        minGain = std::min (minGain, gain);

        const float wL = clampValue (wetDelay[0].process (xL) * gain, -ceil, ceil);
        const float wR = clampValue (wetDelay[1].process (xR) * gain, -ceil, ceil);
        const float dL = dryDelay[0].process (dryL);
        const float dR = dryDelay[1].process (dryR);

        left[i]  = dL + (wL - dL) * mod;
        right[i] = dR + (wR - dR) * mod;

        ++sampleIndex;
    }

    lastBlockGr = target.enabled ? -gainToDb (minGain) : 0.0f;
}

} // namespace prizm::dsp
