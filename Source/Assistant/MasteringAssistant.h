/*
  ==============================================================================

    PRIZM V2 - mastering assistant: analysis-driven chain proposals.

    Workflow (modelled on what Ozone's Master Assistant documents publicly:
    listen -> target -> set EQ / dynamics / limiter -> user accepts or
    cancels), implemented with PRIZM's own algorithms:

      1. Target: the reference track's loudness / tonal balance / width when
         one is loaded, otherwise the requested loudness (e.g. -14 LUFS for
         streaming) and the typical tonal tilt of commercial masters
         (~-5 dB/octave, Pestana et al., AES 2013).
      2. EQ: smoothed difference curve, fitted to PRIZM's shelves and bells.
      3. Compressor: only when the material is dynamic enough to benefit
         (loudness range / PLR), threshold found by simulation so the loud
         parts get the intended gain reduction.
      4. Saturation and imager: conservative, only when there is a reason
         (stated in the proposal).
      5. Limiter: drive found by binary search on an offline simulation of the
         *whole proposed chain* over the captured audio, so the predicted
         integrated loudness is what the user will actually get.

    Runs on PRIZM's background analysis thread, never the audio thread.

  ==============================================================================
*/

#pragma once

#include "../DSP/AudioAnalyzer.h"
#include "../DSP/MasteringChain.h"
#include "Proposal.h"

#include <functional>

namespace prizm::assistant
{

struct MasterRequest
{
    float targetLufs = -14.0f;
    float ceilingDb = -1.0f;
    std::string platform;           // shown in explanations
    bool matchReference = false;
    bool loudnessOnly = false;      // only adjust the limiter to hit targetLufs

    // Style nudges from the chat, -1 .. +1
    float warmth = 0.0f, brightness = 0.0f, punch = 0.0f, width = 0.0f, saturation = 0.0f;
};

struct AudioClip
{
    const float* left = nullptr;
    const float* right = nullptr;
    int numSamples = 0;
    double sampleRate = 48000.0;

    double seconds() const noexcept { return numSamples / sampleRate; }
};

using CancelCheck = std::function<bool()>;

/** Full chain proposal. 'input' is the analysis of 'clip'. */
Proposal buildMasteringProposal (const MasterRequest& request, const ParamState& current,
                                 const dsp::AnalysisResult& input, const dsp::AnalysisResult* reference,
                                 const AudioClip& clip, const CancelCheck& cancelled = {});

/** Simulates 'params' over 'clip' and measures the result. */
Prediction simulate (const ParamState& params, const AudioClip& clip);

/** Fits the EQ gain bands to a desired correction curve (dB on the analysis grid). */
void fitEqToCurve (const std::array<float, dsp::AnalysisResult::numPoints>& curveDb, double sampleRate,
                   float maxGainDb, ParamState& params);

/** Human-readable analysis report (markdown-lite: **bold**, "- " bullets). */
std::string describeAnalysis (const dsp::AnalysisResult& track, const dsp::AnalysisResult* reference,
                              float targetLufs, bool isProcessedOutput);

/** Short comparison lines against a reference (for the reference panel). */
std::vector<std::string> referenceRecommendations (const dsp::AnalysisResult& track, const dsp::AnalysisResult& reference);

/** Finds the most pronounced build-up (relative to the track's own spectral
    trend) between loHz and hiHz. Returns 0 if nothing exceeds minDb. */
float findBuildUp (const dsp::AnalysisResult& track, float loHz, float hiHz, float minDb, float& amountDb);

/** Typical spectral tilt of commercial masters, dB/octave over 100 Hz - 10 kHz. */
constexpr float typicalTiltDbPerOctave = -5.0f;

} // namespace prizm::assistant
