/*
  ==============================================================================

    PRIZM V2 - shared DSP helpers.

    Everything under Source/DSP is plain C++17 with no JUCE dependency so the
    audio engine can be unit-tested offline (see Tests/DSPTests.cpp) and reused
    by the background analysis thread for offline "what-if" simulations.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace prizm::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept           { return std::pow (10.0f, db * 0.05f); }
inline double dbToGain (double db) noexcept         { return std::pow (10.0, db * 0.05); }

inline float gainToDb (float gain, float floorDb = -120.0f) noexcept
{
    return gain > 0.0f ? std::max (floorDb, 20.0f * std::log10 (gain)) : floorDb;
}

inline double powerToDb (double power, double floorDb = -150.0) noexcept
{
    return power > 0.0 ? std::max (floorDb, 10.0 * std::log10 (power)) : floorDb;
}

template <typename T>
inline T clampValue (T v, T lo, T hi) noexcept      { return std::min (hi, std::max (lo, v)); }

//==============================================================================
/** Linear ramp towards a target value. Used for every user-facing parameter so
    that automation, undo/restore and AI-applied changes never produce zipper
    noise or clicks. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, (int) std::lround (sampleRate * rampSeconds));
        reset (target);
    }

    void reset (float value) noexcept
    {
        current = target = value;
        stepsLeft = 0;
    }

    void setTarget (float newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;
        stepsLeft = rampLength;
        step = (target - current) / (float) rampLength;
    }

    float next() noexcept
    {
        if (stepsLeft <= 0)
            return current;

        if (--stepsLeft == 0)
            current = target;
        else
            current += step;

        return current;
    }

    /** Advance by n samples at once (used for control-rate coefficient updates). */
    float skip (int n) noexcept
    {
        if (stepsLeft <= 0)
            return current;

        if (n >= stepsLeft)
        {
            stepsLeft = 0;
            current = target;
        }
        else
        {
            stepsLeft -= n;
            current += step * (float) n;
        }

        return current;
    }

    bool isSmoothing() const noexcept   { return stepsLeft > 0; }
    float getCurrent() const noexcept   { return current; }
    float getTarget() const noexcept    { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int stepsLeft = 0, rampLength = 1;
};

//==============================================================================
/** Fixed-length integer delay line (per channel), allocation-free after prepare. */
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        buffer.assign ((size_t) std::max (1, maxDelay + 1), 0.0f);
        writePos = 0;
    }

    void setDelay (int samples) noexcept    { delay = clampValue (samples, 0, (int) buffer.size() - 1); }
    int getDelay() const noexcept           { return delay; }

    void clear() noexcept                   { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    float process (float x) noexcept
    {
        if (delay == 0)
            return x;

        const int size = (int) buffer.size();
        buffer[(size_t) writePos] = x;
        int readPos = writePos - delay;
        if (readPos < 0)
            readPos += size;

        if (++writePos >= size)
            writePos = 0;

        return buffer[(size_t) readPos];
    }

private:
    std::vector<float> buffer;
    int writePos = 0, delay = 0;
};

//==============================================================================
/** Kaiser window helper (used for FIR design in the oversampler and true-peak
    interpolator). */
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double half = x * 0.5;

    for (int k = 1; k < 64; ++k)
    {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1.0e-12)
            break;
    }

    return sum;
}

inline double kaiserWindow (int n, int length, double beta) noexcept
{
    if (length <= 1)
        return 1.0;

    const double r = (2.0 * n) / (length - 1) - 1.0;
    return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
}

inline double sinc (double x) noexcept
{
    if (std::abs (x) < 1.0e-12)
        return 1.0;

    return std::sin (pi * x) / (pi * x);
}

} // namespace prizm::dsp
