/*
  ==============================================================================

    PRIZM V2 - biquad filters.

    Coefficient formulas follow Robert Bristow-Johnson's "Audio EQ Cookbook".
    Processing is Transposed Direct Form II with double-precision state, which
    stays well-behaved for low-frequency shelves/high-passes at high sample
    rates and tolerates smoothly modulated coefficients.

  ==============================================================================
*/

#pragma once

#include "DspCommon.h"
#include <complex>

namespace prizm::dsp
{

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs identity() noexcept { return {}; }

    static BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        const double inv = 1.0 / a0;
        return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
    }

    static double clampFreq (double sampleRate, double freq) noexcept
    {
        return clampValue (freq, 1.0, sampleRate * 0.49);
    }

    static BiquadCoeffs peak (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * clampFreq (sampleRate, freq) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        return normalise (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                          1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    static BiquadCoeffs lowShelf (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        const double w0 = 2.0 * pi * clampFreq (sampleRate, freq) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        return normalise (A * ((A + 1.0) - (A - 1.0) * cw + 2.0 * sA * alpha),
                          2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                          A * ((A + 1.0) - (A - 1.0) * cw - 2.0 * sA * alpha),
                          (A + 1.0) + (A - 1.0) * cw + 2.0 * sA * alpha,
                          -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                          (A + 1.0) + (A - 1.0) * cw - 2.0 * sA * alpha);
    }

    static BiquadCoeffs highShelf (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
        const double w0 = 2.0 * pi * clampFreq (sampleRate, freq) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        return normalise (A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sA * alpha),
                          -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                          A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sA * alpha),
                          (A + 1.0) - (A - 1.0) * cw + 2.0 * sA * alpha,
                          2.0 * ((A - 1.0) - (A + 1.0) * cw),
                          (A + 1.0) - (A - 1.0) * cw - 2.0 * sA * alpha);
    }

    static BiquadCoeffs highPass (double sampleRate, double freq, double q) noexcept
    {
        const double w0 = 2.0 * pi * clampFreq (sampleRate, freq) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        return normalise ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5,
                          1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    static BiquadCoeffs lowPass (double sampleRate, double freq, double q) noexcept
    {
        const double w0 = 2.0 * pi * clampFreq (sampleRate, freq) / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        return normalise ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
                          1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    /** Complex response at a given frequency. */
    std::complex<double> response (double sampleRate, double freq) const noexcept
    {
        const double w = 2.0 * pi * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    double magnitudeDb (double sampleRate, double freq) const noexcept
    {
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (sampleRate, freq))));
    }
};

/** Butterworth Q values for a 4th-order (24 dB/oct) response built from two biquads. */
constexpr double butterworth4Q1 = 0.54119610014619698;
constexpr double butterworth4Q2 = 1.3065629648763766;

//==============================================================================
/** One biquad section with independent state for up to two channels. */
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) noexcept   { coeffs = c; }
    const BiquadCoeffs& getCoeffs() const noexcept    { return coeffs; }

    void reset() noexcept
    {
        for (auto& s : state)
            s[0] = s[1] = 0.0;
    }

    float process (float x, int channel) noexcept
    {
        auto& s = state[(size_t) channel];
        const double in = x;
        const double y = coeffs.b0 * in + s[0];
        s[0] = coeffs.b1 * in - coeffs.a1 * y + s[1];
        s[1] = coeffs.b2 * in - coeffs.a2 * y;
        return (float) y;
    }

private:
    BiquadCoeffs coeffs;
    double state[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };
};

} // namespace prizm::dsp
