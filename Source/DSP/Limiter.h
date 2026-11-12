/*
  ==============================================================================

    PRIZM V2 - lookahead true-peak brickwall limiter ("maximizer").

    Signal flow per sample (stereo-linked):
      1. apply input gain (how hard the limiter is driven),
      2. detect the (true-)peak level of the incoming signal,
      3. required gain = min (1, ceiling / peak),
      4. sliding-window minimum over the lookahead window (hold),
      5. exponential release (instant attack),
      6. box-filter average over the lookahead window -> smooth attack ramp
         that is guaranteed to reach the required gain before the peak plays,
      7. multiply the delayed audio by that gain; final safety clip at ceiling.

    Latency is fixed (2 ms lookahead + 6 samples detector delay) and does not
    change when the module is switched on/off.

  ==============================================================================
*/

#pragma once

#include "TruePeak.h"

namespace prizm::dsp
{

struct LimiterSettings
{
    bool enabled = false;
    float gainDb = 0.0f;
    float ceilingDb = -1.0f;
    float releaseMs = 100.0f;
    bool truePeak = true;
};

class Limiter
{
public:
    void prepare (double sampleRate);
    void setSettings (const LimiterSettings& s) noexcept;
    void reset (const LimiterSettings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return lookahead + TruePeakDetector::delaySamples; }
    float getGainReductionDb() const noexcept { return lastBlockGr; }

private:
    void pushHold (float value) noexcept;

    double fs = 44100.0;
    LimiterSettings target;
    int lookahead = 88, holdWindow = 90;

    LinearSmoother inGain, ceiling, moduleMix;
    double releaseCoeff = 0.0;

    TruePeakDetector detectors[2];
    DelayLine wetDelay[2], dryDelay[2];

    // Monotonic deque for the sliding minimum.
    std::vector<float> dqValue;
    std::vector<long long> dqIndex;
    int dqHead = 0, dqSize = 0;
    long long sampleIndex = 0;

    // Box filter.
    std::vector<float> boxRing;
    int boxPos = 0;
    double boxSum = 0.0;
    int boxRecalcCounter = 0;

    float releaseState = 1.0f;
    float lastBlockGr = 0.0f;
};

} // namespace prizm::dsp
