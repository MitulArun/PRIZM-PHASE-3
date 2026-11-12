/*
  ==============================================================================

    PRIZM V2 - offline audio analysis.

    Measures everything the assistant and the reference panel compare:
      loudness (integrated / LRA / max short-term), sample & true peak, RMS,
      crest factor and PLR, a level-gated long-term average spectrum (mid and
      side, 1/6-octave smoothed on a fixed 64-point log grid), tonal balance in
      six named bands, stereo correlation, width (side/mid) overall and in the
      low end, L/R balance and spectral tilt.

    Fed incrementally (so a reference file can be streamed from disk on a
    background thread) and is completely independent of the live audio path.

  ==============================================================================
*/

#pragma once

#include "FFT.h"
#include "LoudnessMeter.h"
#include "TruePeak.h"
#include <array>
#include <string>

namespace prizm::dsp
{

struct AnalysisResult
{
    static constexpr int numPoints = 64;
    static constexpr int numBands = 6;

    bool valid = false;
    double sampleRate = 0.0;
    double secondsAnalysed = 0.0;      // audible audio actually analysed

    float integratedLufs = LoudnessMeter::silence;
    float loudnessRange = 0.0f;
    float maxShortTermLufs = LoudnessMeter::silence;
    float samplePeakDb = -144.0f, truePeakDb = -144.0f;
    float rmsDb = -144.0f;
    float crestDb = 0.0f;              // sample peak - RMS
    float plrDb = 0.0f;                // true peak - integrated loudness
    float blockRmsP50 = -144.0f, blockRmsP90 = -144.0f;   // 100 ms RMS percentiles (dBFS)

    float correlation = 1.0f;          // -1 .. +1
    float sideToMidDb = -144.0f;       // stereo width above 150 Hz
    float lowSideToMidDb = -144.0f;    // stereo width below 150 Hz
    float balanceDb = 0.0f;            // L minus R energy

    float tiltDbPerOctave = 0.0f;      // regression of mid spectrum, 100 Hz - 10 kHz

    std::array<float, numPoints> freqs {};
    std::array<float, numPoints> midDb {};    // power density, dB
    std::array<float, numPoints> sideDb {};
    std::array<float, numBands> bandDb {};    // band energy relative to total, dB

    static const char* bandName (int band) noexcept;
    static float bandLowHz (int band) noexcept;
    static float bandHighHz (int band) noexcept;
    static float pointFrequency (int index) noexcept;     // fixed 64-point grid, 20 Hz .. 20 kHz

    /** Spectrum normalised so its mean over 100 Hz - 8 kHz is 0 dB. */
    std::array<float, numPoints> normalisedMid() const noexcept;

    std::string serialise() const;
    static bool deserialise (const std::string& text, AnalysisResult& out);
};

class AudioAnalyzer
{
public:
    void prepare (double sampleRate);
    void process (const float* left, const float* right, int numSamples) noexcept;
    AnalysisResult finish();

    double getSecondsProcessed() const noexcept { return (double) samplesProcessed / fs; }

private:
    void analyseFrame();

    static constexpr int numLevelBuckets = 120;   // 1 dB frame-level buckets, -110 .. +10 dBFS

    struct Bucket
    {
        int frames = 0;
        std::array<double, AnalysisResult::numPoints> mid {}, side {};
        std::array<double, AnalysisResult::numBands> band {};
        double lowMid = 0.0, lowSide = 0.0, highMid = 0.0, highSide = 0.0, total = 0.0;
    };

    double fs = 44100.0;
    long long samplesProcessed = 0;

    LoudnessMeter loudness;
    TruePeakDetector tpL, tpR;
    float samplePeak = 0.0f, truePeak = 0.0f;

    // 100 ms blocks for RMS / correlation (silence excluded)
    int blockLength = 4410, blockCount = 0;
    double blockSumL = 0.0, blockSumR = 0.0, blockSumLR = 0.0;
    double audibleSumSq = 0.0, sumL2 = 0.0, sumR2 = 0.0, sumLR = 0.0;
    long long audibleSamples = 0;
    std::array<int, 240> blockRmsHist {};   // 0.5 dB bins, -110 .. +10 dBFS

    // Spectrum
    FFT fft { 12 };
    int fftSize = 4096, hop = 2048, fifoPos = 0;
    std::vector<float> window, fifoMid, fifoSide;
    std::vector<std::complex<float>> fftData;
    std::vector<double> powerMid, powerSide;
    std::array<int, AnalysisResult::numPoints> pointLo {}, pointHi {};
    std::vector<Bucket> buckets;
};

} // namespace prizm::dsp
