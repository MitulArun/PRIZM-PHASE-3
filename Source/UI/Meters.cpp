/*
  ==============================================================================

    PRIZM V2 - metering components.

  ==============================================================================
*/

#include "Meters.h"
#include "Controls.h"
#include "../DSP/DspCommon.h"

namespace prizm::ui
{

namespace
{
    juce::Colour levelColour (float db)
    {
        if (db > -1.0f)  return Palette::red;
        if (db > -6.0f)  return Palette::amber;
        return Palette::accent;
    }

    juce::String lufsText (float v)
    {
        return v <= -70.0f ? juce::String ("--") : juce::String (v, 1);
    }
}

//==============================================================================
void StereoLevelMeter::setLevels (const float peak[2], const float rms[2])
{
    for (int c = 0; c < 2; ++c)
    {
        const float p = dsp::gainToDb (peak[c], -100.0f);
        const float r = dsp::gainToDb (rms[c], -100.0f);
        peakDb[c] = p > peakDb[c] ? p : peakDb[c] - 1.2f;   // ~36 dB/s fall at 30 Hz
        rmsDb[c] = r;

        if (p >= holdDb[c]) { holdDb[c] = p; holdFrames[c] = 45; }
        else if (--holdFrames[c] < 0) holdDb[c] -= 0.8f;

        maxPeakDb = std::max (maxPeakDb - 0.05f, p);
    }
    repaint();
}

void StereoLevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::textDim);
    g.setFont (font (10.5f, true));
    g.drawText (label, r.removeFromTop (14.0f), juce::Justification::centred);

    const auto numberArea = r.removeFromBottom (14.0f);
    const float barW = (r.getWidth() - 4.0f) * 0.5f;

    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + c * (barW + 4.0f), r.getY(), barW, r.getHeight());
        g.setColour (juce::Colour (0xff111111));   // V1 meter background
        g.fillRoundedRectangle (bar, 3.0f);

        const auto inner = bar.reduced (2.0f);
        const float rmsH = dbToProportion (rmsDb[c]) * inner.getHeight();
        const float peakH = dbToProportion (peakDb[c]) * inner.getHeight();

        g.setColour (levelColour (peakDb[c]).withAlpha (0.35f));
        g.fillRoundedRectangle (inner.withTop (inner.getBottom() - peakH), 2.0f);

        g.setGradientFill (juce::ColourGradient (Palette::red, inner.getX(), inner.getY(),
                                                 Palette::accent, inner.getX(), inner.getBottom(), false));
        g.fillRoundedRectangle (inner.withTop (inner.getBottom() - rmsH), 2.0f);

        if (holdDb[c] > -60.0f)
        {
            const float y = inner.getBottom() - dbToProportion (holdDb[c]) * inner.getHeight();
            g.setColour (holdDb[c] > -0.1f ? Palette::red : juce::Colour (0xffffff00));   // V1's yellow peak line
            g.fillRect (inner.getX(), y - 1.0f, inner.getWidth(), 2.0f);
        }
    }

    g.setColour (maxPeakDb > -0.1f ? Palette::red : Palette::textDim);
    g.setFont (font (10.5f));
    g.drawText (maxPeakDb <= -99.0f ? juce::String ("-inf") : juce::String (maxPeakDb, 1), numberArea, juce::Justification::centred);
}

//==============================================================================
void GainReductionMeter::setGainReduction (float db, bool active)
{
    moduleActive = active;
    grDb += (db - grDb) * (db > grDb ? 0.7f : 0.25f);
    holdDb = std::max (holdDb * 0.985f, grDb);
    repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (moduleActive ? Palette::textDim : Palette::textFaint);
    g.setFont (font (10.5f, true));
    g.drawText (label, r.removeFromTop (14.0f), juce::Justification::centred);

    const auto numberArea = r.removeFromBottom (14.0f);
    auto bar = r.withSizeKeepingCentre (juce::jmin (12.0f, r.getWidth()), r.getHeight());
    g.setColour (juce::Colour (0xff111111));
    g.fillRoundedRectangle (bar, 3.0f);

    const auto inner = bar.reduced (2.0f);
    const float h = juce::jlimit (0.0f, 1.0f, grDb / 12.0f) * inner.getHeight();   // 0..12 dB scale
    g.setColour (Palette::amber);
    g.fillRoundedRectangle (inner.withHeight (h), 2.0f);

    g.setColour (moduleActive ? Palette::text : Palette::textFaint);
    g.setFont (font (10.5f));
    g.drawText (moduleActive ? juce::String (-holdDb, 1) : juce::String ("off"), numberArea, juce::Justification::centred);
}

//==============================================================================
Vectorscope::Vectorscope()
{
    samples.assign (4096 * 2, 0.0f);
    setOpaque (false);
}

void Vectorscope::pushSamples (const float* interleaved, int numFrames)
{
    // Keep only the most recent 4096 frames.
    const int capacity = (int) samples.size() / 2;
    numFrames = std::min (numFrames, capacity);
    const int keep = std::min (count, capacity - numFrames);
    std::memmove (samples.data(), samples.data() + (size_t) (count - keep) * 2, sizeof (float) * (size_t) keep * 2);
    std::memcpy (samples.data() + (size_t) keep * 2, interleaved, sizeof (float) * (size_t) numFrames * 2);
    count = keep + numFrames;
}

void Vectorscope::resized()
{
    trail = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
}

void Vectorscope::update()
{
    if (! trail.isValid())
        return;

    // Fade the previous frames (persistence), then draw the new ones.
    trail.multiplyAllAlphas (0.72f);
    juce::Graphics g (trail);

    const float size = (float) juce::jmin (getWidth(), getHeight());
    const auto centre = getLocalBounds().toFloat().getCentre();

    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
        peak = std::max ({ peak, std::abs (samples[(size_t) i * 2]), std::abs (samples[(size_t) i * 2 + 1]) });

    // Auto-scale slowly so quiet material is still readable.
    const float wanted = 1.0f / juce::jmax (0.05f, peak);
    autoScale += (wanted - autoScale) * (wanted < autoScale ? 0.5f : 0.03f);
    const float scale = 0.46f * size * autoScale * 0.7071f;

    g.setColour (Palette::accent.withAlpha (0.55f));
    for (int i = 0; i < count; ++i)
    {
        const float l = samples[(size_t) i * 2], r = samples[(size_t) i * 2 + 1];
        const float x = centre.x + (r - l) * scale;   // side: right of centre = right channel
        const float y = centre.y - (l + r) * scale;   // mid: up
        g.fillRect (x, y, 1.4f, 1.4f);
    }

    count = 0;
    repaint();
}

void Vectorscope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (r, 4.0f);

    const auto c = r.getCentre();
    const float h = juce::jmin (r.getWidth(), r.getHeight()) * 0.46f;

    g.setColour (Palette::grid);
    g.drawLine (c.x, c.y - h, c.x, c.y + h, 1.0f);                     // M
    g.drawLine (c.x - h, c.y, c.x + h, c.y, 1.0f);                     // S
    g.setColour (Palette::grid.withAlpha (0.6f));
    g.drawLine (c.x - h * 0.7071f, c.y - h * 0.7071f, c.x + h * 0.7071f, c.y + h * 0.7071f, 1.0f);   // L axis
    g.drawLine (c.x + h * 0.7071f, c.y - h * 0.7071f, c.x - h * 0.7071f, c.y + h * 0.7071f, 1.0f);   // R axis

    g.drawImageAt (trail, 0, 0);

    g.setColour (Palette::textFaint);
    g.setFont (font (10.0f, true));
    g.drawText ("L", juce::Rectangle<float> (14, 12).withCentre ({ c.x - h * 0.78f, c.y - h * 0.78f }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (14, 12).withCentre ({ c.x + h * 0.78f, c.y - h * 0.78f }), juce::Justification::centred);
    g.drawText ("M", juce::Rectangle<float> (14, 12).withCentre ({ c.x + 8.0f, c.y - h + 6.0f }), juce::Justification::centred);
}

//==============================================================================
void CorrelationMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto labels = r.removeFromBottom (12.0f);
    auto bar = r.reduced (0.0f, 2.0f);

    g.setColour (juce::Colour (0xff111111));
    g.fillRoundedRectangle (bar, 3.0f);

    const float cx = bar.getCentreX();
    const float x = bar.getX() + (value + 1.0f) * 0.5f * bar.getWidth();
    g.setColour (value < 0.0f ? Palette::red : (value < 0.3f ? Palette::amber : Palette::accent));
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, x), bar.getY() + 2.0f, std::abs (x - cx), bar.getHeight() - 4.0f), 2.0f);
    g.setColour (Palette::grid);
    g.drawVerticalLine ((int) cx, bar.getY(), bar.getBottom());

    g.setColour (Palette::textFaint);
    g.setFont (font (9.5f));
    g.drawText ("-1", labels, juce::Justification::centredLeft);
    g.drawText ("+1", labels, juce::Justification::centredRight);
    g.setColour (Palette::textDim);
    g.drawText ("CORR " + juce::String (value, 2), labels, juce::Justification::centred);
}

//==============================================================================
LoudnessReadout::LoudnessReadout()
{
    setStyle (resetButton, "ghost");
    resetButton.setTooltip ("Reset integrated loudness, loudness range and true-peak max");
    resetButton.onClick = [this] { if (onReset) onReset(); };
    addAndMakeVisible (resetButton);
}

void LoudnessReadout::setValues (const MeterSnapshot& m, float targetLufs, float ceilingDb, bool isComparing)
{
    meters = m;
    target = targetLufs;
    ceiling = ceilingDb;
    comparing = isComparing;
    repaint();
}

void LoudnessReadout::resized()
{
    resetButton.setBounds (getWidth() - 52, 2, 50, 20);
}

void LoudnessReadout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto big = r.removeFromLeft ((int) (r.getWidth() * 0.38f));

    // Integrated loudness vs target
    const float integ = meters.outIntegrated;
    const bool valid = integ > -70.0f;
    const float delta = integ - target;
    const auto colour = ! valid ? Palette::textFaint : (std::abs (delta) <= 1.0f ? Palette::accent : Palette::amber);

    g.setColour (Palette::textDim);
    g.setFont (font (10.5f, true));
    g.drawText ("INTEGRATED", big.removeFromTop (14), juce::Justification::centredLeft);

    g.setColour (colour);
    g.setFont (font (26.0f, true));
    auto valueRow = big.removeFromTop (30);
    g.drawText (lufsText (integ), valueRow, juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (font (11.0f));
    g.drawText ("LUFS", valueRow.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (font (26.0f, true), lufsText (integ)) + 4),
                juce::Justification::bottomLeft);

    g.setFont (font (11.0f));
    g.setColour (Palette::textFaint);
    juce::String targetText = "Target " + juce::String (target, 0);
    if (valid)
        targetText << "   " << (delta >= 0 ? "+" : "") << juce::String (delta, 1) << " LU";
    g.drawText (targetText, big.removeFromTop (16), juce::Justification::centredLeft);

    // Small readouts
    r.removeFromTop (22);   // reset button row
    struct Cell { const char* name; juce::String value; juce::Colour c; };
    const float tp = meters.outTruePeakMaxDb;
    const Cell cells[] = {
        { "SHORT-TERM", lufsText (meters.outShortTerm), Palette::text },
        { "MOMENTARY",  lufsText (meters.outMomentary), Palette::text },
        { "RANGE (LRA)", juce::String (meters.outRange, 1) + " LU", Palette::text },
        { "TRUE PEAK",  tp <= -100.0f ? juce::String ("--") : juce::String (tp, 1) + " dBTP", tp > ceiling + 0.1f ? Palette::red : Palette::text },
    };

    const int cw = r.getWidth() / 2, ch = r.getHeight() / 2;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % 2) * cw, r.getY() + (i / 2) * ch, cw, ch);
        g.setColour (Palette::textFaint);
        g.setFont (font (9.5f, true));
        g.drawText (cells[i].name, cell.removeFromTop (12), juce::Justification::centredLeft);
        g.setColour (cells[i].c);
        g.setFont (font (14.0f, true));
        g.drawText (cells[i].value, cell.removeFromTop (18), juce::Justification::centredLeft);
    }

    if (comparing)
    {
        g.setColour (Palette::amber);
        g.setFont (font (10.0f, true));
        g.drawText ("METERING PROCESSED SIGNAL", getLocalBounds().removeFromTop (14).withTrimmedRight (58), juce::Justification::centredRight);
    }
}

//==============================================================================
MetersPanel::MetersPanel()
{
    for (auto* c : std::initializer_list<juce::Component*> { &loudness, &inMeter, &outMeter, &compGr, &limGr, &scope, &correlation })
        addAndMakeVisible (c);
}

void MetersPanel::update (const MeterSnapshot& m, float targetLufs, float ceilingDb, bool compOn, bool limOn, bool comparing)
{
    loudness.setValues (m, targetLufs, ceilingDb, comparing);
    inMeter.setLevels (m.inPeak, m.inRms);
    outMeter.setLevels (m.outPeak, m.outRms);
    compGr.setGainReduction (compOn ? m.compGrDb : 0.0f, compOn);
    limGr.setGainReduction (limOn ? m.limGrDb : 0.0f, limOn);
    correlation.setCorrelation (m.correlation);
    scope.update();
}

void MetersPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    drawSectionTitle (g, getLocalBounds().reduced (12, 6).removeFromTop (16), "Loudness & Levels", "BS.1770 / EBU R128");
}

void MetersPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    r.removeFromTop (20);

    loudness.setBounds (r.removeFromTop (88));
    r.removeFromTop (8);

    const int scopeSize = juce::jmin (r.getHeight() - 16, r.getWidth() / 2);
    auto scopeArea = r.removeFromRight (scopeSize);
    correlation.setBounds (scopeArea.removeFromBottom (26));
    scope.setBounds (scopeArea.withSizeKeepingCentre (scopeArea.getWidth(), juce::jmin (scopeArea.getHeight(), scopeArea.getWidth())));

    r.removeFromRight (10);
    const int w = r.getWidth() / 6;
    inMeter.setBounds (r.removeFromLeft (w * 2).reduced (2, 0));
    outMeter.setBounds (r.removeFromLeft (w * 2).reduced (2, 0));
    compGr.setBounds (r.removeFromLeft (w).reduced (1, 0));
    limGr.setBounds (r.reduced (1, 0));
}

} // namespace prizm::ui
