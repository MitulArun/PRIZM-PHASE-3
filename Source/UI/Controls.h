/*
  ==============================================================================

    PRIZM V2 - small parameter-bound controls.

    Every control is attached to a real AudioProcessorValueTreeState
    parameter, so moving it changes the audio (and is automatable / undoable).

  ==============================================================================
*/

#pragma once

#include "../Common/ParamTable.h"
#include "PrizmLookAndFeel.h"

namespace prizm::ui
{

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, int paramIndex, const juce::String& labelText,
          juce::Colour colour = Palette::accent)
        : label (labelText)
    {
        const auto& info = paramInfo (paramIndex);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.getProperties().set ("knobColour", colour.toString());
        slider.setDoubleClickReturnValue (true, info.defaultValue);
        slider.setTooltip (juce::String (info.name) + " - double-click to reset");
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, info.id, slider);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (isEnabled() ? Palette::textDim : Palette::textFaint);
        g.setFont (font (11.5f, true));
        g.drawFittedText (label, getLocalBounds().removeFromTop (15), juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (15);
        slider.setBounds (r);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 16);
    }

    juce::Slider slider;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& state, int paramIndex, const juce::String& text, bool powerStyle = false)
        : juce::ToggleButton (text)
    {
        if (powerStyle)
            getProperties().set (PrizmLookAndFeel::styleKey, "power");
        setTooltip (paramInfo (paramIndex).name);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramInfo (paramIndex).id, *this);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState& state, int paramIndex)
    {
        addItemList (juce::StringArray::fromTokens (paramInfo (paramIndex).choices, "|", ""), 1);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramInfo (paramIndex).id, *this);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Helper: give a TextButton one of the look-and-feel styles. */
inline void setStyle (juce::Button& b, const char* style)
{
    b.getProperties().set (PrizmLookAndFeel::styleKey, style);
}

} // namespace prizm::ui
