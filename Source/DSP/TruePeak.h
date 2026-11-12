/*
  ==============================================================================

    PRIZM V2 - true-peak (inter-sample peak) detector.

    4x polyphase windowed-sinc interpolation in the spirit of ITU-R BS.1770-4
    Annex 2. The 49-tap prototype is a Nyquist filter, so phase 0 reproduces
    the original samples exactly and phases 1-3 fill in the points between
    them. Each call returns the largest magnitude over the interval
    [n - 6, n - 5.25] (i.e. the detector has a fixed delay of 6 samples).

  ==============================================================================
*/

#pragma once

#include "DspCommon.h"

namespace prizm::dsp
{

class TruePeakDetector
{
public:
    static constexpr int delaySamples = 6;

    TruePeakDetector() { design(); }

    void reset() noexcept
    {
        std::fill (std::begin (hist), std::end (hist), 0.0f);
        pos = 0;
    }

    /** Returns the max |value| of the 4 interpolated points ending ~5.25 samples ago. */
    float process (float x) noexcept
    {
        pos = (pos == 0 ? histLen : pos) - 1;
        hist[pos] = hist[pos + histLen] = x;
        const float* w = hist + pos;      // w[j] = x[n - j]

        float peak = std::abs (w[delaySamples]);   // phase 0 == x[n-6] exactly

        for (int p = 1; p < 4; ++p)
        {
            const float* taps = phaseTaps[p];
            float acc = 0.0f;
            for (int j = 0; j < tapsPerPhase; ++j)
                acc += taps[j] * w[j];
            peak = std::max (peak, std::abs (acc));
        }

        return peak;
    }

    /** Sample-peak-only variant with the same delay (used when true peak is off). */
    float processSamplePeak (float x) noexcept
    {
        pos = (pos == 0 ? histLen : pos) - 1;
        hist[pos] = hist[pos + histLen] = x;
        return std::abs (hist[pos + delaySamples]);
    }

private:
    static constexpr int prototypeLength = 49;
    static constexpr int tapsPerPhase = 13;
    static constexpr int histLen = 13;

    void design() noexcept
    {
        const int centre = (prototypeLength - 1) / 2;

        for (int p = 0; p < 4; ++p)
        {
            double sum = 0.0;
            for (int j = 0; j < tapsPerPhase; ++j)
            {
                const int n = 4 * j + p;
                double h = 0.0;
                if (n < prototypeLength)
                    h = sinc ((n - centre) / 4.0) * kaiserWindow (n, prototypeLength, 6.0);
                phaseTaps[p][j] = (float) h;
                sum += h;
            }

            for (int j = 0; j < tapsPerPhase; ++j)
                phaseTaps[p][j] = (float) (phaseTaps[p][j] / sum);
        }

        reset();
    }

    float phaseTaps[4][tapsPerPhase] {};
    float hist[histLen * 2] {};
    int pos = 0;
};

} // namespace prizm::dsp
