/*
  ==============================================================================

    PRIZM V2 - plugin editor.

    Keeps V1's identity (dark gradient, green "PRIZM / AI Audio Assistant"
    header, spectrum analyser, L/R meters, "AI CHAT" panel, PrizmPrototype
    footer) and grows it into the V2 workspace:

      header : A/B (Original / Processed), level match, mono check,
               undo / redo / history
      left   : spectrum + interactive EQ, then the mastering chain or the
               reference panel
      right  : loudness / level / gain-reduction meters + vectorscope,
               then the AI chat

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/ChatPanel.h"
#include "UI/Meters.h"
#include "UI/ModuleRack.h"
#include "UI/PrizmLookAndFeel.h"
#include "UI/ReferencePanel.h"
#include "UI/SpectrumDisplay.h"

//==============================================================================
/**
*/
class Prizmv1AudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
{
public:
    Prizmv1AudioProcessorEditor (Prizmv1AudioProcessor&);
    ~Prizmv1AudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void openReferenceChooser();
    void showHistoryMenu();
    void updateHeaderButtons();
    void showTab (bool reference);

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    Prizmv1AudioProcessor& audioProcessor;

    prizm::ui::PrizmLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    prizm::ui::SpectrumDisplay spectrum;
    prizm::ui::ModuleRack rack;
    prizm::ui::ReferencePanel referencePanel;
    prizm::ui::MetersPanel meters;
    prizm::ui::ChatPanel chat;

    juce::TextButton tabChain { "Mastering Chain" }, tabReference { "Reference" };
    juce::TextButton originalButton { "Original" }, processedButton { "Processed" };
    juce::TextButton levelMatchButton { "Level match" }, monoButton { "Mono" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, historyButton { "History" };

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> fifoScratch;
    int lastReferenceVersion = -1;
    bool referenceTab = false;
    juce::Rectangle<int> headerArea, footerArea, spectrumTitle, lowerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Prizmv1AudioProcessorEditor)
};
