/*
  ==============================================================================

    PRIZM V2 - ITU-R BS.1770-4 / EBU R128 loudness meter.

      - K-weighting (pre-filter shelf + RLB high-pass), coefficients derived
        for the actual sample rate (not just the 48 kHz table in the spec),
      - 100 ms sub-blocks; momentary = 400 ms, short-term = 3 s windows,
      - integrated loudness with absolute (-70 LUFS) and relative (-10 LU)
        gating over 400 ms blocks with 75% overlap,
      - loudness range (EBU Tech 3342): short-term values, -20 LU relative
        gate, 95th minus 10th percentile.

    Gating uses 0.1 LU histograms that also accumulate exact block energies,
    so memory is constant regardless of how long a session is metered.

  ==============================================================================
*/

#pragma once

#include "DspCommon.h"
#include <array>

namespace prizm::dsp
{

class LoudnessMeter
{
public:
    static constexpr float silence = -144.0f;

    void prepare (double sampleRate);
    void reset() noexcept;              // everything
    void resetIntegrated() noexcept;    // integrated, LRA and max values only

    void process (const float* left, const float* right, int numSamples) noexcept;

    float getMomentary() const noexcept      { return momentary; }
    float getShortTerm() const noexcept      { return shortTerm; }
    float getIntegrated() const noexcept     { return integrated; }
    float getLoudnessRange() const noexcept  { return loudnessRange; }
    float getMaxMomentary() const noexcept   { return maxMomentary; }
    float getMaxShortTerm() const noexcept   { return maxShortTerm; }

    /** Number of 400 ms gating blocks that passed the absolute gate. */
    int getNumGatedBlocks() const noexcept   { return totalBlocks; }

    static float energyToLufs (double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : silence;
    }

private:
    static constexpr int numBins = 800;          // -70 .. +10 LUFS in 0.1 LU steps
    static constexpr float histMin = -70.0f, binWidth = 0.1f;

    struct Histogram
    {
        std::array<int, numBins> count {};
        std::array<double, numBins> energy {};

        void clear() noexcept { count.fill (0); energy.fill (0.0); }
        void add (float lufs, double meanSquare) noexcept;
    };

    struct KFilter
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };

        double process (double x, int ch) noexcept
        {
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }

        void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    };

    void finishSubBlock() noexcept;
    void updateIntegrated() noexcept;
    void updateLoudnessRange() noexcept;

    double fs = 48000.0;
    KFilter shelf, highpass;

    int subBlockLength = 4800, subBlockCount = 0;
    double subBlockSum = 0.0;

    std::array<double, 30> subBlocks {};   // ring of the last 3 s of sub-block mean squares
    int subBlockWrite = 0, subBlocksFilled = 0;

    Histogram gatingHist, shortTermHist;
    int totalBlocks = 0;

    float momentary = silence, shortTerm = silence, integrated = silence, loudnessRange = 0.0f;
    float maxMomentary = silence, maxShortTerm = silence;
};

} // namespace prizm::dsp
