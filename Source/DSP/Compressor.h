/*
  ==============================================================================

    PRIZM V2 - stereo-linked feed-forward bus compressor.

    Design follows Giannoulis, Massberg & Reiss, "Digital Dynamic Range
    Compressor Design - A Tutorial and Analysis" (JAES 60(6), 2012):
      - RMS-style level detection (5 ms) on an optional high-passed sidechain,
        so bass doesn't make the whole mix pump,
      - soft-knee static gain computer in the log domain,
      - smooth decoupled peak detector for attack/release on gain reduction.
    Zero latency; parallel (dry/wet) mix; click-free enable/disable.

  ==============================================================================
*/

#pragma once

#include "Biquad.h"

namespace prizm::dsp
{

struct CompressorSettings
{
    bool enabled = false;
    float thresholdDb = -18.0f;
    float ratio = 2.0f;
    float attackMs = 20.0f;
    float releaseMs = 200.0f;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
    float mixPercent = 100.0f;
    float detectorHpfHz = 20.0f;   // <= 20 Hz means "off"
};

class Compressor
{
public:
    void prepare (double sampleRate);
    void setSettings (const CompressorSettings& s) noexcept;
    void reset (const CompressorSettings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    /** Largest gain reduction applied during the last process() call (positive dB). */
    float getGainReductionDb() const noexcept { return lastBlockGr; }

    /** Static gain computer: output level for a given detector level (both dB),
        excluding makeup. */
    static float gainComputerDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

private:
    void updateTimeConstants() noexcept;

    double fs = 44100.0;
    CompressorSettings target;

    LinearSmoother threshold, knee, makeup, mix, moduleMix;
    float ratio = 2.0f;

    double attackCoeff = 0.0, releaseCoeff = 0.0, rmsCoeff = 0.0;
    double rmsState = 0.0, peakState = 0.0, grState = 0.0;

    Biquad detectorHpf;
    float currentHpfHz = -1.0f;
    float lastBlockGr = 0.0f;
};

} // namespace prizm::dsp
