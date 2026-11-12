/*
  ==============================================================================

    PRIZM V2 - spectrum analyser with interactive EQ curve.

  ==============================================================================
*/

#include "SpectrumDisplay.h"
#include "../DSP/ParametricEQ.h"
#include "../DSP/MasteringChain.h"
#include "../PluginProcessor.h"

namespace prizm::ui
{

namespace
{
    constexpr int ringSize = 16384;
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
}

SpectrumDisplay::SpectrumDisplay (Prizmv1AudioProcessor& p) : processor (p)
{
    nodes = {
        { 0, pEqHpfFreq, -1,        -1,       pEqHpfOn, "Low Cut" },
        { 1, pEqLsFreq,  pEqLsGain, pEqLsQ,   -1,       "Low Shelf" },
        { 2, pEqP1Freq,  pEqP1Gain, pEqP1Q,   -1,       "Bell 1" },
        { 3, pEqP2Freq,  pEqP2Gain, pEqP2Q,   -1,       "Bell 2" },
        { 4, pEqP3Freq,  pEqP3Gain, pEqP3Q,   -1,       "Bell 3" },
        { 5, pEqHsFreq,  pEqHsGain, pEqHsQ,   -1,       "High Shelf" },
        { 6, pEqLpfFreq, -1,        -1,       pEqLpfOn, "High Cut" },
    };

    inRing.assign (ringSize, 0.0f);
    outRing.assign (ringSize, 0.0f);

    for (int i = 0; i < numPoints; ++i)
        freqs[(size_t) i] = minFreq * std::pow (maxFreq / minFreq, (float) i / (numPoints - 1));

    inDb.fill (minDb);
    outDb.fill (minDb);
    refDb.fill (minDb);
    eqDb.fill (0.0f);
    setOpaque (false);
}

void SpectrumDisplay::pushSamples (const float* interleaved, int numFrames)
{
    for (int i = 0; i < numFrames; ++i)
    {
        inRing[(size_t) ringPos] = interleaved[i * 2];
        outRing[(size_t) ringPos] = interleaved[i * 2 + 1];
        ringPos = (ringPos + 1) % ringSize;
    }
    newFrames += numFrames;
}

void SpectrumDisplay::setReference (const dsp::AnalysisResult* reference)
{
    hasReference = reference != nullptr && reference->valid;
    if (! hasReference)
    {
        repaint();
        return;
    }

    // Resample the reference's 64-point long-term spectrum onto the display grid (log interpolation).
    for (int i = 0; i < numPoints; ++i)
    {
        const float pos = std::log (freqs[(size_t) i] / 20.0f) / std::log (1000.0f) * (dsp::AnalysisResult::numPoints - 1);
        const int k = juce::jlimit (0, dsp::AnalysisResult::numPoints - 2, (int) std::floor (pos));
        const float frac = juce::jlimit (0.0f, 1.0f, pos - (float) k);
        const float v = reference->midDb[(size_t) k] + (reference->midDb[(size_t) k + 1] - reference->midDb[(size_t) k]) * frac;
        refDb[(size_t) i] = v + tiltDbPerOct * std::log2 (freqs[(size_t) i] / 1000.0f);
    }

    refOffset = std::numeric_limits<float>::quiet_NaN();
    repaint();
}

void SpectrumDisplay::computeSpectrum (const std::vector<float>& ring, std::array<float, numPoints>& out, bool& initialised)
{
    const int n = fft.getSize();
    for (int i = 0; i < n; ++i)
    {
        const int idx = (ringPos - n + i + ringSize) % ringSize;
        fftBuffer[(size_t) i] = { ring[(size_t) idx] * window[(size_t) i], 0.0f };
    }

    fft.perform (fftBuffer.data());

    const double fs = processor.getCurrentSampleRate();
    const double binHz = fs / n;
    const float norm = 20.0f * std::log10 (n / 4.0f);   // full-scale sine reads 0 dB (as in V1)
    const int maxBin = n / 2 - 1;

    for (int p = 0; p < numPoints; ++p)
    {
        const double f = freqs[(size_t) p];
        int lo = (int) std::ceil (f / std::pow (2.0, 1.0 / 12.0) / binHz);
        int hi = (int) std::floor (f * std::pow (2.0, 1.0 / 12.0) / binHz);
        if (hi < lo)
            lo = hi = (int) std::lround (f / binHz);
        lo = juce::jlimit (1, maxBin, lo);
        hi = juce::jlimit (1, maxBin, hi);

        double power = 0.0;
        for (int k = lo; k <= hi; ++k)
            power += std::norm (fftBuffer[(size_t) k]);
        power /= (hi - lo + 1);

        const float db = (float) (10.0 * std::log10 (power + 1.0e-20)) - norm + tiltDbPerOct * std::log2 ((float) f / 1000.0f);
        const float clamped = juce::jlimit (minDb - 20.0f, maxDb + 6.0f, db);
        auto& v = out[(size_t) p];

        if (! initialised)
            v = clamped;
        else
            v += (clamped - v) * (clamped > v ? 0.6f : 0.12f);   // fast attack, slow release
    }

    initialised = true;
}

void SpectrumDisplay::update (bool audioActive)
{
    // FFT size follows the sample rate so the frequency resolution stays ~10 Hz.
    const int wantedOrder = processor.getCurrentSampleRate() > 50000.0 ? 13 : 12;
    if (wantedOrder != fftOrder || window.empty())
    {
        fftOrder = wantedOrder;
        fft.setOrder (fftOrder);
        window = dsp::FFT::hann (fft.getSize());
        fftBuffer.assign ((size_t) fft.getSize(), {});
    }

    if (newFrames > 0)
    {
        computeSpectrum (inRing, inDb, inInit);
        computeSpectrum (outRing, outDb, outInit);
        newFrames = 0;
    }
    else if (! audioActive)
    {
        for (auto* arr : { &inDb, &outDb })
            for (auto& v : *arr)
                v += (minDb - 20.0f - v) * 0.08f;   // decay when nothing is playing
    }

    showHint = ! audioActive && outDb[(size_t) numPoints / 2] < minDb + 5.0f;

    // Keep the reference curve level-matched to the live output (shape is what matters).
    if (hasReference)
    {
        double a = 0.0, b = 0.0;
        int n = 0;
        for (int i = 0; i < numPoints; ++i)
        {
            if (freqs[(size_t) i] >= 100.0f && freqs[(size_t) i] <= 8000.0f)
            {
                a += outDb[(size_t) i];
                b += refDb[(size_t) i];
                ++n;
            }
        }

        const float liveMean = audioActive && outInit ? (float) (a / n) : -45.0f;
        const float target = liveMean - (float) (b / n);
        refOffset = std::isnan (refOffset) ? target : refOffset + (target - refOffset) * 0.05f;
    }

    // Live EQ response from the current parameter values.
    const auto settings = dsp::makeEQSettings (processor.getParamState());
    const double fs = processor.getCurrentSampleRate();
    for (int i = 0; i < numPoints; ++i)
        eqDb[(size_t) i] = (float) dsp::ParametricEQ::responseDb (settings, fs, freqs[(size_t) i]);

    repaint();
}

//==============================================================================
juce::Rectangle<float> SpectrumDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f).withTrimmedRight (26.0f).withTrimmedTop (4.0f);
}

void SpectrumDisplay::resized()  { cachedPlot = plotArea(); }

float SpectrumDisplay::freqToX (float f) const
{
    return cachedPlot.getX() + std::log (f / minFreq) / std::log (maxFreq / minFreq) * cachedPlot.getWidth();
}

float SpectrumDisplay::xToFreq (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - cachedPlot.getX()) / cachedPlot.getWidth());
    return minFreq * std::pow (maxFreq / minFreq, t);
}

float SpectrumDisplay::dbToY (float db) const   { return juce::jmap (db, minDb, maxDb, cachedPlot.getBottom(), cachedPlot.getY()); }
float SpectrumDisplay::eqDbToY (float db) const { return juce::jmap (db, -eqRange, eqRange, cachedPlot.getBottom(), cachedPlot.getY()); }
float SpectrumDisplay::yToEqDb (float y) const  { return juce::jmap (y, cachedPlot.getBottom(), cachedPlot.getY(), -eqRange, eqRange); }

float SpectrumDisplay::param (int index) const  { return processor.getParamState()[(size_t) index]; }

void SpectrumDisplay::setParam (int index, float value)
{
    auto* p = processor.getParam (index);
    p->setValueNotifyingHost (p->convertTo0to1 (clampParam (index, value)));
}

juce::Point<float> SpectrumDisplay::nodePosition (const Node& n) const
{
    const float f = param (n.freqParam);
    const float y = n.gainParam >= 0 ? eqDbToY (param (n.gainParam)) : eqDbToY (0.0f);
    return { freqToX (f), y };
}

int SpectrumDisplay::nodeAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto& n = nodes[(size_t) i];
        if (n.enableParam >= 0 && param (n.enableParam) < 0.5f)
            continue;
        const float d = nodePosition (n).getDistanceFrom (pos);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

//==============================================================================
void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto plot = cachedPlot;

    // Grid (V1 style)
    g.setFont (font (10.5f));
    for (float db = 0.0f; db >= minDb; db -= 18.0f)
    {
        const float y = dbToY (db);
        g.setColour (Palette::grid.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Palette::textFaint);
        g.drawText (juce::String ((int) db), (int) bounds.getX() + 2, (int) y - 7, 26, 14, juce::Justification::centredRight);
    }

    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (Palette::textFaint.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), (int) plot.getRight() + 2, (int) eqDbToY (db) - 7, 24, 14,
                    juce::Justification::centredLeft);
    }

    const float gridFreqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char* gridLabels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const float x = freqToX (gridFreqs[i]);
        g.setColour (Palette::grid.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Palette::textFaint);
        g.drawText (gridLabels[i], (int) x - 16, (int) plot.getBottom() + 1, 32, 14, juce::Justification::centred);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    auto makePath = [&] (const std::array<float, numPoints>& data, float offset)
    {
        juce::Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = freqToX (freqs[(size_t) i]);
            const float y = dbToY (data[(size_t) i] + offset);
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    };

    // Input spectrum
    if (inInit)
    {
        g.setColour (Palette::inputTrace.withAlpha (0.75f));
        g.strokePath (makePath (inDb, 0.0f), juce::PathStrokeType (1.2f));
    }

    // Output spectrum (V1's green gradient fill + line)
    if (outInit)
    {
        auto line = makePath (outDb, 0.0f);
        juce::Path fill (line);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x7700ff88), plot.getX(), plot.getY(),
                                                 juce::Colour (0x0800ff88), plot.getX(), plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Palette::accentSoft);
        g.strokePath (line, juce::PathStrokeType (1.6f));
    }

    // Reference
    if (hasReference && ! std::isnan (refOffset))
    {
        juce::Path dashed;
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.8f).createDashedStroke (dashed, makePath (refDb, refOffset), dashes, 2);
        g.setColour (Palette::reference.withAlpha (0.9f));
        g.fillPath (dashed);
    }

    // EQ curve
    const bool eqOn = param (pEqOn) >= 0.5f;
    {
        juce::Path curve;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = freqToX (freqs[(size_t) i]);
            const float y = eqDbToY (juce::jlimit (-eqRange - 6.0f, eqRange + 6.0f, eqDb[(size_t) i]));
            if (i == 0) curve.startNewSubPath (x, y);
            else        curve.lineTo (x, y);
        }

        juce::Path area (curve);
        area.lineTo (plot.getRight(), eqDbToY (0.0f));
        area.lineTo (plot.getX(), eqDbToY (0.0f));
        area.closeSubPath();

        g.setColour (juce::Colours::white.withAlpha (eqOn ? 0.07f : 0.03f));
        g.fillPath (area);
        g.setColour (juce::Colours::white.withAlpha (eqOn ? 0.9f : 0.3f));
        g.strokePath (curve, juce::PathStrokeType (eqOn ? 2.0f : 1.4f));
    }

    g.restoreState();

    // Nodes
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto& n = nodes[(size_t) i];
        if (n.enableParam >= 0 && param (n.enableParam) < 0.5f)
            continue;

        const auto pos = nodePosition (n);
        const auto colour = Palette::bandColour (n.band);
        const float r = (i == dragNode || i == hoverNode) ? 8.0f : 6.5f;
        g.setColour (colour.withAlpha (eqOn ? 1.0f : 0.4f));
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos));
        g.setColour (juce::Colour (0xff111111));
        g.setFont (font (9.5f, true));
        const juce::String tag = n.band == 0 ? "L" : (n.band == 6 ? "H" : juce::String (n.band));
        g.drawText (tag, juce::Rectangle<float> (r * 2, r * 2).withCentre (pos), juce::Justification::centred);
    }

    // Hover / drag readout
    const int shown = dragNode >= 0 ? dragNode : hoverNode;
    if (shown >= 0)
    {
        const auto& n = nodes[(size_t) shown];
        juce::String text = juce::String (n.name) + "  " + formatParamValue (n.freqParam, param (n.freqParam));
        if (n.gainParam >= 0) text << "  " << formatParamValue (n.gainParam, param (n.gainParam));
        if (n.qParam >= 0)    text << "  Q " << juce::String (param (n.qParam), 2);

        const auto pos = nodePosition (n);
        auto box = juce::Rectangle<float> (juce::GlyphArrangement::getStringWidth (font (12.0f), text) + 16.0f, 22.0f)
                       .withCentre ({ pos.x, pos.y - 22.0f })
                       .constrainedWithin (plot);
        g.setColour (juce::Colour (0xee202020));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Palette::bandColour (n.band));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (Palette::text);
        g.setFont (font (12.0f));
        g.drawText (text, box, juce::Justification::centred);
    }

    // Legend
    {
        struct Item { juce::Colour c; const char* label; bool show; };
        const Item items[] = { { Palette::inputTrace, "Input", true }, { Palette::accentSoft, "Output", true },
                               { Palette::reference, "Reference", hasReference }, { juce::Colours::white, eqOn ? "EQ" : "EQ (off)", true } };
        float x = plot.getRight() - 8.0f;
        g.setFont (font (11.0f));
        for (int i = 3; i >= 0; --i)
        {
            if (! items[i].show)
                continue;
            const float w = juce::GlyphArrangement::getStringWidth (font (11.0f), items[i].label) + 16.0f;
            x -= w;
            g.setColour (items[i].c);
            g.fillRoundedRectangle (x, plot.getY() + 7.0f, 8.0f, 3.0f, 1.5f);
            g.setColour (Palette::textDim);
            g.drawText (items[i].label, (int) x + 11, (int) plot.getY() + 1, (int) w, 14, juce::Justification::centredLeft);
        }

        g.setColour (Palette::textFaint);
        g.drawText ("+3 dB/oct", (int) plot.getX() + 6, (int) plot.getY() + 1, 80, 14, juce::Justification::centredLeft);
    }

    if (showHint)
    {
        g.setColour (Palette::textFaint);
        g.setFont (font (13.0f));
        g.drawText ("Play audio to see its spectrum - drag the numbered dots to EQ", plot, juce::Justification::centred);
    }
}

//==============================================================================
void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n != hoverNode)
    {
        hoverNode = n;
        setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverNode = -1;
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragNode = nodeAt (e.position);
    if (dragNode < 0)
        return;

    const auto& n = nodes[(size_t) dragNode];
    processor.getParam (n.freqParam)->beginChangeGesture();
    if (n.gainParam >= 0)
        processor.getParam (n.gainParam)->beginChangeGesture();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNode < 0)
        return;

    const auto& n = nodes[(size_t) dragNode];
    setParam (n.freqParam, xToFreq (e.position.x));
    if (n.gainParam >= 0)
        setParam (n.gainParam, std::round (yToEqDb (e.position.y) * 10.0f) / 10.0f);
    repaint();
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragNode < 0)
        return;

    const auto& n = nodes[(size_t) dragNode];
    processor.getParam (n.freqParam)->endChangeGesture();
    if (n.gainParam >= 0)
        processor.getParam (n.gainParam)->endChangeGesture();
    dragNode = -1;
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = nodeAt (e.position);
    if (i < 0)
        return;

    const auto& n = nodes[(size_t) i];
    const int target = n.gainParam >= 0 ? n.gainParam : n.enableParam;
    auto* p = processor.getParam (target);
    p->beginChangeGesture();
    setParam (target, 0.0f);   // gain to 0 dB, or filter off
    p->endChangeGesture();
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int i = nodeAt (e.position);
    if (i < 0 || nodes[(size_t) i].qParam < 0)
        return;

    const int qp = nodes[(size_t) i].qParam;
    auto* p = processor.getParam (qp);
    p->beginChangeGesture();
    setParam (qp, param (qp) * (wheel.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
    p->endChangeGesture();
}

} // namespace prizm::ui
