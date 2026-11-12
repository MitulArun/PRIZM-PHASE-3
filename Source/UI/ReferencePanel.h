/*
  ==============================================================================

    PRIZM V2 - reference track panel.

    Load a reference, see how your track compares (loudness, dynamics, tonal
    balance, stereo width) and ask the assistant to match it. The reference is
    analysed on the background thread and never routed to the plugin output.

  ==============================================================================
*/

#pragma once

#include "../DSP/AudioAnalyzer.h"
#include "Controls.h"

class Prizmv1AudioProcessor;

namespace prizm::ui
{

class ReferencePanel : public juce::Component
{
public:
    explicit ReferencePanel (Prizmv1AudioProcessor& processor);

    void update();   // editor timer
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLoadReference;
    std::function<void (const juce::String&)> onSendChat;

private:
    void refresh();
    const dsp::AnalysisResult* trackAnalysis() const;

    Prizmv1AudioProcessor& processor;
    juce::TextButton loadButton { "Load reference..." }, clearButton { "Clear" };
    juce::TextButton analyseButton { "Analyze my track" }, matchButton { "Match reference" };
    juce::TextButton sourceInput { "Unprocessed" }, sourceOutput { "Processed" };

    int lastReferenceVersion = -1, lastChatVersion = -1;
    bool showOutput = false, hasReference = false;
    juce::String referenceName;
    dsp::AnalysisResult reference;
    std::vector<std::string> recommendations;
    juce::String status;
    float progress = -1.0f;

    juce::Rectangle<int> infoArea, tableArea, barsArea, recsArea;
};

} // namespace prizm::ui
