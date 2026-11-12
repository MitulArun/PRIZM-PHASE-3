/*
  ==============================================================================

    PRIZM V2 - metering: loudness readouts, level / gain-reduction meters,
    vectorscope (goniometer) and correlation meter.

    All values come from the processor's real-time measurements
    (Engine/MeterData.h) or its scope FIFO - nothing is simulated.

  ==============================================================================
*/

#pragma once

#include "../Engine/MeterData.h"
#include "PrizmLookAndFeel.h"

namespace prizm::ui
{

/** Vertical stereo level meter (V1's LevelMeter, now with dB scale, colour zones and numeric peak). */
class StereoLevelMeter : public juce::Component
{
public:
    explicit StereoLevelMeter (juce::String labelText) : label (std::move (labelText)) {}
    void setLevels (const float peak[2], const float rms[2]);
    void paint (juce::Graphics&) override;

    static float dbToProportion (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); }

private:
    juce::String label;
    float peakDb[2] { -100.0f, -100.0f }, rmsDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f };
    int holdFrames[2] {};
    float maxPeakDb = -100.0f;
};

/** Gain reduction bar (grows downward), for compressor and limiter. */
class GainReductionMeter : public juce::Component
{
public:
    explicit GainReductionMeter (juce::String labelText) : label (std::move (labelText)) {}
    void setGainReduction (float db, bool active);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    float grDb = 0.0f, holdDb = 0.0f;
    bool moduleActive = false;
};

/** Goniometer: L/R rotated 45 degrees so mono is vertical, with persistence. */
class Vectorscope : public juce::Component
{
public:
    Vectorscope();
    void pushSamples (const float* interleaved, int numFrames);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<float> samples;   // interleaved L/R, latest first-in-first-out
    int count = 0;
    juce::Image trail;
    float autoScale = 1.0f;
};

class CorrelationMeter : public juce::Component
{
public:
    void setCorrelation (float c) { value += (c - value) * 0.25f; repaint(); }
    void paint (juce::Graphics&) override;

private:
    float value = 1.0f;
};

/** Loudness numbers: integrated (vs target), short-term, momentary, LRA, true peak. */
class LoudnessReadout : public juce::Component
{
public:
    LoudnessReadout();
    void setValues (const MeterSnapshot& m, float targetLufs, float ceilingDb, bool comparing);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onReset;

private:
    MeterSnapshot meters;
    float target = -14.0f, ceiling = -1.0f;
    bool comparing = false;
    juce::TextButton resetButton { "Reset" };
};

/** The full meter section shown above the chat. */
class MetersPanel : public juce::Component
{
public:
    MetersPanel();
    void update (const MeterSnapshot& m, float targetLufs, float ceilingDb, bool compOn, bool limOn, bool comparing);
    void pushScopeSamples (const float* interleaved, int numFrames) { scope.pushSamples (interleaved, numFrames); }
    void paint (juce::Graphics&) override;
    void resized() override;

    LoudnessReadout loudness;

private:
    StereoLevelMeter inMeter { "IN" }, outMeter { "OUT" };
    GainReductionMeter compGr { "COMP" }, limGr { "LIM" };
    Vectorscope scope;
    CorrelationMeter correlation;
};

} // namespace prizm::ui
