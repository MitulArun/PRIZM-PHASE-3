/*
  ==============================================================================

    PRIZM V2 - spectrum analyser with interactive EQ curve.

    Evolves V1's SpectrumAnalyzer (same dB grid, frequency labels and green
    gradient fill) into:
      - input (grey) and processed output (green) spectra from real FFTs of
        the audio, using the host's actual sample rate (V1 assumed 44.1 kHz),
      - the loaded reference track's long-term spectrum (amber, level-matched),
      - the live EQ response with draggable band nodes (drag = freq/gain,
        mouse wheel = Q, double-click = reset).

  ==============================================================================
*/

#pragma once

#include "../DSP/AudioAnalyzer.h"
#include "../DSP/FFT.h"
#include "PrizmLookAndFeel.h"

class Prizmv1AudioProcessor;

namespace prizm::ui
{

class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (Prizmv1AudioProcessor& processor);

    /** Interleaved frames: channel 0 = input (mono), channel 1 = processed (mono). */
    void pushSamples (const float* interleaved, int numFrames);

    /** Called by the editor's timer (~30 Hz). */
    void update (bool audioActive);

    void setReference (const dsp::AnalysisResult* reference);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Node
    {
        int band;             // colour index
        int freqParam, gainParam, qParam, enableParam;
        const char* name;
    };

    static constexpr int numPoints = 256;
    static constexpr float minDb = -90.0f, maxDb = 0.0f, eqRange = 12.0f, tiltDbPerOct = 3.0f;

    juce::Rectangle<float> plotArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float eqDbToY (float db) const;
    float yToEqDb (float y) const;
    float param (int index) const;
    void setParam (int index, float value);
    juce::Point<float> nodePosition (const Node& n) const;
    int nodeAt (juce::Point<float> p) const;
    void computeSpectrum (const std::vector<float>& ring, std::array<float, numPoints>& out, bool& initialised);

    Prizmv1AudioProcessor& processor;
    std::vector<Node> nodes;

    dsp::FFT fft { 12 };
    int fftOrder = 12;
    std::vector<float> window;
    std::vector<std::complex<float>> fftBuffer;
    std::vector<float> inRing, outRing;
    int ringPos = 0, newFrames = 0;

    std::array<float, numPoints> freqs {}, inDb {}, outDb {}, refDb {}, eqDb {};
    bool inInit = false, outInit = false, hasReference = false;
    float refOffset = 0.0f;
    bool showHint = true;

    int dragNode = -1, hoverNode = -1;
    juce::Rectangle<float> cachedPlot;
};

} // namespace prizm::ui
