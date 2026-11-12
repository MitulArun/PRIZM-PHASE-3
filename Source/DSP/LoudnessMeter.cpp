/*
  ==============================================================================

    PRIZM V2 - ITU-R BS.1770-4 / EBU R128 loudness meter.

  ==============================================================================
*/

#include "LoudnessMeter.h"

namespace prizm::dsp
{

void LoudnessMeter::Histogram::add (float lufs, double meanSquare) noexcept
{
    const int bin = clampValue ((int) std::floor ((lufs - histMin) / binWidth), 0, numBins - 1);
    ++count[(size_t) bin];
    energy[(size_t) bin] += meanSquare;
}

void LoudnessMeter::prepare (double sampleRate)
{
    fs = sampleRate;

    // Stage 1: high-shelf "pre-filter" modelling the acoustic effect of the head.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }

    // Stage 2: RLB high-pass.
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        highpass.b0 = 1.0;
        highpass.b1 = -2.0;
        highpass.b2 = 1.0;
        highpass.a1 = 2.0 * (K * K - 1.0) / a0;
        highpass.a2 = (1.0 - K / Q + K * K) / a0;
    }

    subBlockLength = std::max (1, (int) std::lround (fs * 0.1));
    reset();
}

void LoudnessMeter::reset() noexcept
{
    shelf.clear();
    highpass.clear();
    subBlockCount = 0;
    subBlockSum = 0.0;
    subBlocks.fill (0.0);
    subBlockWrite = subBlocksFilled = 0;
    momentary = shortTerm = silence;
    resetIntegrated();
}

void LoudnessMeter::resetIntegrated() noexcept
{
    gatingHist.clear();
    shortTermHist.clear();
    totalBlocks = 0;
    integrated = silence;
    loudnessRange = 0.0f;
    maxMomentary = maxShortTerm = silence;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double l = highpass.process (shelf.process (left[i], 0), 0);
        const double r = highpass.process (shelf.process (right[i], 1), 1);
        subBlockSum += l * l + r * r;   // channel weights are 1.0 for L and R

        if (++subBlockCount >= subBlockLength)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock() noexcept
{
    subBlocks[(size_t) subBlockWrite] = subBlockSum / subBlockCount;
    subBlockWrite = (subBlockWrite + 1) % (int) subBlocks.size();
    subBlocksFilled = std::min (subBlocksFilled + 1, (int) subBlocks.size());
    subBlockSum = 0.0;
    subBlockCount = 0;

    auto meanOfLast = [this] (int n)
    {
        double sum = 0.0;
        for (int k = 1; k <= n; ++k)
            sum += subBlocks[(size_t) ((subBlockWrite - k + (int) subBlocks.size()) % (int) subBlocks.size())];
        return sum / n;
    };

    if (subBlocksFilled >= 4)
    {
        const double e = meanOfLast (4);
        momentary = energyToLufs (e);
        maxMomentary = std::max (maxMomentary, momentary);

        if (momentary > -70.0f)
        {
            gatingHist.add (momentary, e);
            ++totalBlocks;
            updateIntegrated();
        }
    }
    else
    {
        momentary = energyToLufs (meanOfLast (subBlocksFilled));
    }

    if (subBlocksFilled >= 30)
    {
        const double e = meanOfLast (30);
        shortTerm = energyToLufs (e);
        maxShortTerm = std::max (maxShortTerm, shortTerm);

        if (shortTerm > -70.0f)
        {
            shortTermHist.add (shortTerm, e);
            updateLoudnessRange();
        }
    }
    else
    {
        // Not 3 s of audio yet: show the loudness of what we have so the meter isn't blank.
        shortTerm = energyToLufs (meanOfLast (subBlocksFilled));
    }
}

void LoudnessMeter::updateIntegrated() noexcept
{
    double energy = 0.0;
    long long n = 0;
    for (int b = 0; b < numBins; ++b)
    {
        energy += gatingHist.energy[(size_t) b];
        n += gatingHist.count[(size_t) b];
    }

    if (n == 0)
    {
        integrated = silence;
        return;
    }

    const float relativeGate = energyToLufs (energy / (double) n) - 10.0f;

    energy = 0.0;
    n = 0;
    for (int b = 0; b < numBins; ++b)
    {
        const float centre = histMin + (b + 0.5f) * binWidth;
        if (centre >= relativeGate)
        {
            energy += gatingHist.energy[(size_t) b];
            n += gatingHist.count[(size_t) b];
        }
    }

    integrated = n > 0 ? energyToLufs (energy / (double) n) : silence;
}

void LoudnessMeter::updateLoudnessRange() noexcept
{
    double energy = 0.0;
    long long n = 0;
    for (int b = 0; b < numBins; ++b)
    {
        energy += shortTermHist.energy[(size_t) b];
        n += shortTermHist.count[(size_t) b];
    }

    if (n == 0)
        return;

    const float relativeGate = energyToLufs (energy / (double) n) - 20.0f;

    int firstBin = 0;
    long long gatedCount = 0;
    for (int b = numBins - 1; b >= 0; --b)
    {
        if (histMin + (b + 0.5f) * binWidth < relativeGate)
        {
            firstBin = b + 1;
            break;
        }
        gatedCount += shortTermHist.count[(size_t) b];
    }

    if (gatedCount < 2)
    {
        loudnessRange = 0.0f;
        return;
    }

    auto percentile = [&] (double p)
    {
        const long long rank = (long long) std::ceil (p * (double) gatedCount);
        long long seen = 0;
        for (int b = firstBin; b < numBins; ++b)
        {
            seen += shortTermHist.count[(size_t) b];
            if (seen >= rank)
                return histMin + (b + 0.5f) * binWidth;
        }
        return histMin + (numBins - 0.5f) * binWidth;
    };

    loudnessRange = std::max (0.0f, percentile (0.95) - percentile (0.10));
}

} // namespace prizm::dsp
