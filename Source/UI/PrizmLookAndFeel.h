/*
  ==============================================================================

    PRIZM V2 - look and feel.

    Grows V1's CustomLookAndFeel (dark panels, #00ff88 accent, blue send
    button, thin rounded scrollbars) into a full style for knobs, toggles,
    segmented buttons, combo boxes and menus.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace prizm::ui
{

namespace Palette
{
    const juce::Colour bgTop       { 0xff2a2a2a };   // V1 background gradient
    const juce::Colour bgBottom    { 0xff151515 };
    const juce::Colour panel       { 0xff1e1e1e };   // V1 panel colour
    const juce::Colour panelRaised { 0xff252525 };
    const juce::Colour border      { 0xff303030 };   // V1 outline
    const juce::Colour borderHi    { 0xff505050 };   // V1 focused outline
    const juce::Colour grid        { 0xff3a3a3a };
    const juce::Colour text        { 0xffe6e6e6 };
    const juce::Colour textDim     { 0xffaaaaaa };   // V1 label grey
    const juce::Colour textFaint   { 0xff777777 };
    const juce::Colour accent      { 0xff00ff88 };   // PRIZM green (V1)
    const juce::Colour accentSoft  { 0xff55ffaa };   // V1 spectrum line
    const juce::Colour blue        { 0xff0055dd };   // V1 send button
    const juce::Colour blueBubble  { 0xff1f4fc4 };
    const juce::Colour amber       { 0xffffb020 };
    const juce::Colour red         { 0xffff5050 };
    const juce::Colour reference   { 0xffffaa33 };
    const juce::Colour inputTrace  { 0xff8a8a8a };

    juce::Colour bandColour (int band);   // EQ band colours (0 = low cut ... 6 = high cut)
}

juce::Font font (float size, bool bold = false);

/** Draws a V1-style panel (rounded, filled, thin border). */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 6.0f);

/** Small upper-case section title like V1's "SPECTRUM ANALYZER". */
void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& subtitle = {});

class PrizmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PrizmLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    /** Button property keys understood by this look and feel. */
    static constexpr const char* styleKey = "prizmStyle";   // "primary", "accent", "ghost", "segment", "power", "chip"
};

} // namespace prizm::ui
