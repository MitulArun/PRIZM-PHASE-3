/*
  ==============================================================================

    PRIZM V2 - the complete mastering chain.

        input trim -> EQ -> compressor -> saturation -> imager -> limiter
        -> output trim -> [monitor: original/processed A/B, mono check]

    The chain has a constant latency (saturation oversampling + limiter
    lookahead) whatever modules are enabled, so the host's delay compensation
    never has to change. The A/B monitor compares against the *unprocessed*
    input delayed by exactly that latency, with a short crossfade and optional
    loudness matching, so switching is seamless and fair.

    The same class is used live (audio thread) and offline by the assistant to
    simulate proposals on captured audio before the user hears them.

  ==============================================================================
*/

#pragma once

#include "../Common/ParamTable.h"
#include "Compressor.h"
#include "Limiter.h"
#include "ParametricEQ.h"
#include "Saturator.h"
#include "StereoImager.h"

namespace prizm::dsp
{

struct ChainSettings
{
    float inGainDb = 0.0f, outGainDb = 0.0f;
    EQSettings eq;
    CompressorSettings comp;
    SaturatorSettings sat;
    ImagerSettings img;
    LimiterSettings lim;
};

ChainSettings makeChainSettings (const ParamState& p) noexcept;
EQSettings makeEQSettings (const ParamState& p) noexcept;

struct MonitorSettings
{
    bool listenToOriginal = false;   // A/B: hear the unprocessed input
    float originalGainDb = 0.0f;     // level-match gain applied to the original
    bool monoCheck = false;          // sum output to mono (monitoring only)
};

class MasteringChain
{
public:
    static constexpr int chunkSize = 512;

    void prepare (double sampleRate);
    void setSettings (const ChainSettings& s) noexcept;
    void reset (const ChainSettings& s) noexcept;      // snap all smoothing, clear state
    void setMonitor (const MonitorSettings& m) noexcept;

    /** Processes any block size in place (internally split into chunks).
        If tap pointers are given they receive the processed signal *before*
        the A/B / mono monitor stage (what the meters and analysis measure). */
    void process (float* left, float* right, int numSamples,
                  float* tapLeft = nullptr, float* tapRight = nullptr) noexcept;

    /** Processes up to (but excluding) the limiter and output trim. Used by the
        assistant to evaluate several limiter settings on the same material. */
    void processPreLimiter (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return saturator.getLatencySamples() + limiter.getLatencySamples(); }
    double getSampleRate() const noexcept  { return fs; }

    float getCompressorGainReductionDb() const noexcept { return compressor.getGainReductionDb(); }
    float getLimiterGainReductionDb() const noexcept    { return limiter.getGainReductionDb(); }

    Limiter& getLimiter() noexcept { return limiter; }

private:
    void processChunk (float* left, float* right, int n, bool includeLimiter,
                       float* tapLeft = nullptr, float* tapRight = nullptr) noexcept;

    double fs = 44100.0;
    ParametricEQ eq;
    Compressor compressor;
    Saturator saturator;
    StereoImager imager;
    Limiter limiter;

    LinearSmoother inGain, outGain;
    LinearSmoother originalMix, originalGain, monoMix;
    DelayLine originalDelay[2];

    float dryL[chunkSize] {}, dryR[chunkSize] {};
};

} // namespace prizm::dsp
