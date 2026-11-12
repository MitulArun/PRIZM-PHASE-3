/*
  ==============================================================================

    PRIZM V2 - look and feel.

  ==============================================================================
*/

#include "PrizmLookAndFeel.h"

namespace prizm::ui
{

juce::Colour Palette::bandColour (int band)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xff9aa4b1),   // low cut
        juce::Colour (0xffff7a59),   // low shelf
        juce::Colour (0xffffc145),   // bell 1
        juce::Colour (0xff4fd1c5),   // bell 2
        juce::Colour (0xff6ea8ff),   // bell 3
        juce::Colour (0xffc58bff),   // high shelf
        juce::Colour (0xff9aa4b1),   // high cut
    };
    return colours[juce::jlimit (0, 6, band)];
}

juce::Font font (float size, bool bold)
{
    auto opts = juce::FontOptions (size);
    return juce::Font (bold ? opts.withStyle ("Bold") : opts);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& subtitle)
{
    g.setColour (Palette::textDim);
    g.setFont (font (12.0f, true));
    g.drawText (title.toUpperCase(), r, juce::Justification::centredLeft);

    if (subtitle.isNotEmpty())
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (font (12.0f, true), title.toUpperCase());
        g.setColour (Palette::textFaint);
        g.setFont (font (11.0f));
        g.drawText (subtitle, r.withTrimmedLeft (w + 8), juce::Justification::centredLeft);
    }
}

//==============================================================================
PrizmLookAndFeel::PrizmLookAndFeel()
{
    // V1 colours
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1e1e1e));
    setColour (juce::TextEditor::textColourId, juce::Colour (0xffcccccc));
    setColour (juce::TextEditor::outlineColourId, Palette::border);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::borderHi);
    setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.25f));
    setColour (juce::CaretComponent::caretColourId, Palette::accent);
    setColour (juce::TextButton::buttonColourId, Palette::blue);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ScrollBar::thumbColourId, Palette::border);
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    // V2 additions
    setColour (juce::Slider::textBoxTextColourId, Palette::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Palette::accent.withAlpha (0.3f));
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::Label::textWhenEditingColourId, Palette::text);
    setColour (juce::Label::backgroundWhenEditingColourId, Palette::panelRaised);
    setColour (juce::Label::outlineWhenEditingColourId, Palette::borderHi);
    setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
    setColour (juce::ComboBox::textColourId, Palette::text);
    setColour (juce::ComboBox::outlineColourId, Palette::border);
    setColour (juce::ComboBox::arrowColourId, Palette::textDim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff222222));
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, Palette::text);
    setColour (juce::ToggleButton::tickColourId, Palette::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Palette::textFaint);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2b2b2b));
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TooltipWindow::outlineColourId, Palette::borderHi);
}

void PrizmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.14f);
    const float arcRadius = radius - lineW * 0.5f;
    const bool enabled = slider.isEnabled();
    const auto colourProp = slider.getProperties()["knobColour"];
    const auto colour = colourProp.isVoid() ? Palette::accent : juce::Colour::fromString (colourProp.toString());

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::grid);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters (e.g. EQ gain) fill from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zeroPos * (endAngle - startAngle);
    const float a1 = startAngle + pos * (endAngle - startAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    g.setColour (enabled ? colour : Palette::textFaint);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body
    const float bodyR = arcRadius - lineW * 1.1f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff222222), centre.x, centre.y + bodyR, false));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));

    juce::Path pointer;
    const float pLen = bodyR * 0.75f;
    pointer.addRoundedRectangle (-1.25f, -bodyR + 2.0f, 2.5f, pLen * 0.55f, 1.2f);
    g.setColour (enabled ? Palette::text : Palette::textFaint);
    g.fillPath (pointer, juce::AffineTransform::rotation (a1).translated (centre));
}

void PrizmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool isHighlighted, bool isDown)
{
    const auto style = button.getProperties()[styleKey].toString();
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = style == "chip" ? bounds.getHeight() * 0.5f : 5.0f;
    const bool on = button.getToggleState();

    juce::Colour fill, outline = juce::Colours::transparentBlack;

    if (style == "accent")
        fill = Palette::accent.withAlpha (button.isEnabled() ? 0.9f : 0.3f);
    else if (style == "ghost" || style == "chip")
    {
        fill = on ? Palette::accent.withAlpha (0.18f) : Palette::panelRaised;
        outline = on ? Palette::accent.withAlpha (0.6f) : Palette::border;
    }
    else if (style == "segment")
    {
        fill = on ? Palette::accent.withAlpha (0.2f) : Palette::panelRaised;
        outline = on ? Palette::accent : Palette::border;
    }
    else   // "primary" / default: V1's blue Send button
        fill = button.isEnabled() ? juce::Colour (0xff0055dd) : Palette::panelRaised;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    if (! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    if (isDown)
    {
        g.setColour (juce::Colour (0x33000000));
        g.fillRoundedRectangle (bounds, radius);
    }
    else if (isHighlighted && button.isEnabled())
    {
        g.setColour (juce::Colour (0x18ffffff));
        g.fillRoundedRectangle (bounds, radius);
    }
}

void PrizmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto style = button.getProperties()[styleKey].toString();
    juce::Colour c = Palette::text;

    if (style == "accent")
        c = juce::Colour (0xff0b1f15);
    else if ((style == "segment" || style == "ghost" || style == "chip") && button.getToggleState())
        c = Palette::accent;
    else if (style == "chip")
        c = Palette::textDim;

    if (! button.isEnabled())
        c = c.withAlpha (0.4f);

    g.setColour (c);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

juce::Font PrizmLookAndFeel::getTextButtonFont (juce::TextButton& button, int height)
{
    const auto style = button.getProperties()[styleKey].toString();
    return font (juce::jmin (14.0f, height * 0.5f), style == "accent" || style == "primary" || style.isEmpty());
}

void PrizmLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool)
{
    const auto style = button.getProperties()[styleKey].toString();
    const bool on = button.getToggleState();

    if (style == "power")
    {
        // Module power switch: a small circle with a power glyph.
        auto r = button.getLocalBounds().toFloat().reduced (2.0f);
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        auto c = juce::Rectangle<float> (s, s).withCentre (r.getCentre());

        g.setColour (on ? Palette::accent.withAlpha (0.2f) : Palette::panelRaised);
        g.fillEllipse (c);
        g.setColour (on ? Palette::accent : Palette::textFaint);
        g.drawEllipse (c.reduced (0.5f), 1.0f);

        juce::Path glyph;
        const auto ic = c.reduced (s * 0.27f);
        glyph.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.5f, ic.getHeight() * 0.5f, 0.0f,
                             juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi * 0.22f, true);
        glyph.startNewSubPath (ic.getCentreX(), ic.getY() - 1.0f);
        glyph.lineTo (ic.getCentreX(), ic.getCentreY());
        g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    // Default: rounded checkbox + label
    auto bounds = button.getLocalBounds();
    const int box = juce::jmin (16, bounds.getHeight() - 4);
    auto boxR = bounds.removeFromLeft (box + 6).withSizeKeepingCentre (box, box).toFloat();

    g.setColour (on ? Palette::accent.withAlpha (0.2f) : Palette::panelRaised);
    g.fillRoundedRectangle (boxR, 3.0f);
    g.setColour (on ? Palette::accent : (isHighlighted ? Palette::borderHi : Palette::textFaint));
    g.drawRoundedRectangle (boxR.reduced (0.5f), 3.0f, 1.0f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (boxR.getX() + box * 0.22f, boxR.getCentreY());
        tick.lineTo (boxR.getX() + box * 0.42f, boxR.getBottom() - box * 0.25f);
        tick.lineTo (boxR.getRight() - box * 0.2f, boxR.getY() + box * 0.25f);
        g.setColour (Palette::accent);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.isEnabled() ? Palette::text : Palette::textFaint);
        g.setFont (font (13.0f));
        g.drawFittedText (button.getButtonText(), bounds, juce::Justification::centredLeft, 2);
    }
}

void PrizmLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int, int width, int,
                                      bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                      bool isMouseOver, bool)
{
    // V1 style: thin rounded thumb on the right.
    if (isScrollbarVertical)
    {
        g.setColour (isMouseOver ? Palette::borderHi : Palette::border);
        g.fillRoundedRectangle ((float) (x + width - 6), (float) thumbStartPosition, 4.0f, (float) thumbSize, 2.0f);
    }
}

void PrizmLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 6.0f);
}

void PrizmLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? Palette::accent.withAlpha (0.6f)
                                                : editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 6.0f, 1.0f);
}

void PrizmLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Palette::borderHi : Palette::border);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (Palette::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

juce::Font PrizmLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return font (13.0f); }
juce::Font PrizmLookAndFeel::getPopupMenuFont()                 { return font (14.0f); }

juce::Label* PrizmLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (font (11.5f));
    l->setColour (juce::Label::textColourId, Palette::textDim);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

} // namespace prizm::ui
