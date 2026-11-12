/*
  ==============================================================================

    PRIZM V2 - harmonic saturation (4x oversampled).

    Curves (normalised so low-level signals pass at unity gain, i.e. drive adds
    harmonics and gently rounds peaks instead of just making things louder):
      Tape      : symmetric tanh -> odd harmonics, soft peak rounding
      Tube      : biased tanh    -> adds even harmonics (DC removed)
      Soft Clip : cubic soft clipper with a firmer knee

    Dry/wet mixing happens in the oversampled domain so both paths share the
    same linear-phase filters. When disabled the module still delays audio by
    its (constant) latency so host delay compensation never changes.

  ==============================================================================
*/

#pragma once

#include "Oversampler.h"

namespace prizm::dsp
{

struct SaturatorSettings
{
    bool enabled = false;
    int type = 0;              // 0 Tape, 1 Tube, 2 Soft Clip
    float driveDb = 6.0f;
    float mixPercent = 50.0f;
};

class Saturator
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setSettings (const SaturatorSettings& s) noexcept;
    void reset (const SaturatorSettings& s) noexcept;

    /** numSamples must be <= maxBlockSize given to prepare(). */
    void process (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return oversampler.getLatencySamples(); }

    /** The transfer curve at 'drive' (dB), for UI display and tests. */
    static float shape (int type, float x, float driveGain) noexcept;

private:
    double fs = 44100.0;
    SaturatorSettings target;
    Oversampler4x oversampler;
    DelayLine dryDelay[2];
    LinearSmoother drive, mix, moduleMix;
    int currentType = 0, pendingType = 0;
    LinearSmoother typeFade;     // fades the wet path out/in when the curve type changes
    double dcState[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };
    double dcCoeff = 0.0;
    std::vector<float> wetBuffer[2];
};

} // namespace prizm::dsp
