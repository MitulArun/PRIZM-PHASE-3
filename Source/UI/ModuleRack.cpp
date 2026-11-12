/*
  ==============================================================================

    PRIZM V2 - mastering chain strip and module control pages.

  ==============================================================================
*/

#include "ModuleRack.h"
#include "../DSP/Compressor.h"
#include "../DSP/Saturator.h"
#include "../PluginProcessor.h"

namespace prizm::ui
{

//==============================================================================
/** One stage in the chain strip: name, live status, optional power switch. */
class ModuleRack::Pill : public juce::Component
{
public:
    Pill (juce::AudioProcessorValueTreeState& state, const juce::String& n, int enableParam, Page p)
        : name (n), page (p)
    {
        if (enableParam >= 0)
        {
            power = std::make_unique<ParamToggle> (state, enableParam, "", true);
            addAndMakeVisible (*power);
        }
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setStatus (const juce::String& s, bool active)
    {
        if (s != status || active != isActive)
        {
            status = s;
            isActive = active;
            repaint();
        }
    }

    void setSelected (bool s) { selected = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (selected ? Palette::accent.withAlpha (0.12f) : Palette::panelRaised);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (selected ? Palette::accent : (hover ? Palette::borderHi : Palette::border));
        g.drawRoundedRectangle (r, 6.0f, selected ? 1.5f : 1.0f);

        auto text = getLocalBounds().reduced (10, 6);
        if (power != nullptr)
            text.removeFromRight (24);

        g.setColour (isActive ? Palette::text : Palette::textFaint);
        g.setFont (font (12.5f, true));
        g.drawFittedText (name.toUpperCase(), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, 1);
        g.setColour (isActive ? Palette::accentSoft : Palette::textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText (status, text, juce::Justification::topLeft, 1);
    }

    void resized() override
    {
        if (power != nullptr)
            power->setBounds (getLocalBounds().removeFromRight (30).withSizeKeepingCentre (22, 22));
    }

    void mouseUp (const juce::MouseEvent& e) override        { if (e.mouseWasClicked() && onClick) onClick (page); }
    void mouseEnter (const juce::MouseEvent&) override       { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override        { hover = false; repaint(); }

    std::function<void (Page)> onClick;
    Page page;

private:
    juce::String name, status;
    bool isActive = true, selected = false, hover = false;
    std::unique_ptr<ParamToggle> power;
};

//==============================================================================
class ModuleRack::PageBase : public juce::Component
{
public:
    PageBase (juce::String t, juce::String d) : title (std::move (t)), description (std::move (d)) {}

    virtual void update (const ParamState&, const MeterSnapshot&) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (14, 8);
        g.setColour (Palette::text);
        g.setFont (font (14.0f, true));
        g.drawText (title, r.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (Palette::textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (description, r.removeFromTop (18), juce::Justification::centredLeft, 1);
        paintExtra (g);
    }

    virtual void paintExtra (juce::Graphics&) {}

    juce::Rectangle<int> content() const { return getLocalBounds().reduced (14, 8).withTrimmedTop (42); }

protected:
    juce::String title, description;
};

//==============================================================================
namespace
{
    using Page = ModuleRack::Page;

    /** Lays knobs out in a grid of 'columns'. */
    void layoutGrid (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int columns, int maxKnob = 92)
    {
        const int n = (int) items.size();
        const int rows = (n + columns - 1) / columns;
        const int cellW = area.getWidth() / columns;
        const int cellH = juce::jmin (area.getHeight() / juce::jmax (1, rows), maxKnob + 20);
        int i = 0;
        for (auto* c : items)
        {
            const int col = i % columns, row = i / columns;
            auto cell = juce::Rectangle<int> (area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
            c->setBounds (cell.withSizeKeepingCentre (juce::jmin (cellW - 6, maxKnob), cellH - 4));
            ++i;
        }
    }

    //--------------------------------------------------------------------------
    class GainPage : public ModuleRack::PageBase
    {
    public:
        GainPage (Prizmv1AudioProcessor& p)
            : PageBase ("Input & Output", "Input trim sets how hard the chain is driven; output trim is applied after the limiter."),
              processor (p), in (p.apvts, pInGain, "INPUT"), out (p.apvts, pOutGain, "OUTPUT")
        {
            addAndMakeVisible (in);
            addAndMakeVisible (out);
        }

        void resized() override
        {
            auto r = content();
            layoutGrid (r.removeFromLeft (juce::jmin (r.getWidth(), 260)), { &in, &out }, 2);
        }

        void paintExtra (juce::Graphics& g) override
        {
            auto r = content().withTrimmedLeft (280);
            const double fs = processor.getCurrentSampleRate();
            const int lat = processor.getChainLatencySamples();
            g.setColour (Palette::textDim);
            g.setFont (font (12.5f));
            g.drawFittedText ("Processing latency: " + juce::String (lat) + " samples (" + juce::String (1000.0 * lat / fs, 1)
                                  + " ms) - reported to your DAW for automatic delay compensation.\n"
                                  "Latency stays constant when modules are switched on or off.",
                              r.removeFromTop (60), juce::Justification::topLeft, 4);
        }

    private:
        Prizmv1AudioProcessor& processor;
        Knob in, out;
    };

    //--------------------------------------------------------------------------
    class EQPage : public ModuleRack::PageBase
    {
    public:
        EQPage (Prizmv1AudioProcessor& p)
            : PageBase ("Equalizer", "Shapes the tone. Drag the numbered dots in the spectrum, or use the knobs (double-click resets).")
        {
            auto& s = p.apvts;
            columns = {
                { "LOW CUT", 0, { new ParamToggle (s, pEqHpfOn, "On"), new Knob (s, pEqHpfFreq, "FREQ", Palette::bandColour (0)) } },
                { "LOW SHELF", 1, { new Knob (s, pEqLsFreq, "FREQ", Palette::bandColour (1)), new Knob (s, pEqLsGain, "GAIN", Palette::bandColour (1)), new Knob (s, pEqLsQ, "Q", Palette::bandColour (1)) } },
                { "BELL 1", 2, { new Knob (s, pEqP1Freq, "FREQ", Palette::bandColour (2)), new Knob (s, pEqP1Gain, "GAIN", Palette::bandColour (2)), new Knob (s, pEqP1Q, "Q", Palette::bandColour (2)) } },
                { "BELL 2", 3, { new Knob (s, pEqP2Freq, "FREQ", Palette::bandColour (3)), new Knob (s, pEqP2Gain, "GAIN", Palette::bandColour (3)), new Knob (s, pEqP2Q, "Q", Palette::bandColour (3)) } },
                { "BELL 3", 4, { new Knob (s, pEqP3Freq, "FREQ", Palette::bandColour (4)), new Knob (s, pEqP3Gain, "GAIN", Palette::bandColour (4)), new Knob (s, pEqP3Q, "Q", Palette::bandColour (4)) } },
                { "HIGH SHELF", 5, { new Knob (s, pEqHsFreq, "FREQ", Palette::bandColour (5)), new Knob (s, pEqHsGain, "GAIN", Palette::bandColour (5)), new Knob (s, pEqHsQ, "Q", Palette::bandColour (5)) } },
                { "HIGH CUT", 6, { new ParamToggle (s, pEqLpfOn, "On"), new Knob (s, pEqLpfFreq, "FREQ", Palette::bandColour (6)) } },
            };

            for (auto& c : columns)
                for (auto* comp : c.controls)
                {
                    owned.add (comp);
                    addAndMakeVisible (comp);
                }
        }

        void resized() override
        {
            auto r = content();
            const int colW = r.getWidth() / (int) columns.size();
            for (size_t i = 0; i < columns.size(); ++i)
            {
                auto col = juce::Rectangle<int> (r.getX() + (int) i * colW, r.getY() + 20, colW, r.getHeight() - 20).reduced (3, 0);
                columnBounds[i] = col.withY (r.getY()).withHeight (r.getHeight());
                const int n = (int) columns[i].controls.size();
                const int h = juce::jmin (col.getHeight() / 3, 86);
                for (int k = 0; k < n; ++k)
                {
                    auto* c = columns[i].controls[(size_t) k];
                    auto cell = col.removeFromTop (dynamic_cast<juce::ToggleButton*> (c) != nullptr ? 26 : h);
                    c->setBounds (dynamic_cast<juce::ToggleButton*> (c) != nullptr ? cell.withSizeKeepingCentre (54, 22)
                                                                                   : cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 78), cell.getHeight()));
                }
            }
        }

        void paintExtra (juce::Graphics& g) override
        {
            for (size_t i = 0; i < columns.size(); ++i)
            {
                auto head = columnBounds[i].withHeight (16);
                g.setColour (Palette::bandColour (columns[i].band));
                g.fillEllipse ((float) head.getX() + 6.0f, (float) head.getCentreY() - 4.0f, 8.0f, 8.0f);
                g.setColour (Palette::textDim);
                g.setFont (font (11.0f, true));
                g.drawFittedText (columns[i].name, head.withTrimmedLeft (18), juce::Justification::centredLeft, 1);
                if (i > 0)
                {
                    g.setColour (Palette::border);
                    g.drawVerticalLine (columnBounds[i].getX() - 3, (float) columnBounds[i].getY(), (float) columnBounds[i].getBottom());
                }
            }
        }

    private:
        struct Column { const char* name; int band; std::vector<juce::Component*> controls; };
        std::vector<Column> columns;
        std::array<juce::Rectangle<int>, 7> columnBounds;
        juce::OwnedArray<juce::Component> owned;
    };

    //--------------------------------------------------------------------------
    /** Static curve display: compressor (dB in/out) or saturation waveshaper. */
    class CurveView : public juce::Component
    {
    public:
        enum class Mode { Compressor, Saturation };
        explicit CurveView (Mode m) : mode (m) {}

        void setState (const ParamState& p, float liveGr)
        {
            state = p;
            gr = liveGr;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (r, 4.0f);
            auto plot = r.reduced (8.0f);

            g.setColour (Palette::grid);
            g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);   // 1:1
            g.drawRect (plot, 1.0f);

            juce::Path curve;
            const int n = 100;
            for (int i = 0; i <= n; ++i)
            {
                float x, y;
                if (mode == Mode::Compressor)
                {
                    const float in = -60.0f + 60.0f * i / n;
                    const float out = dsp::Compressor::gainComputerDb (in, state[pCompThresh], state[pCompRatio], state[pCompKnee]) + state[pCompMakeup];
                    x = plot.getX() + (in + 60.0f) / 60.0f * plot.getWidth();
                    y = plot.getBottom() - juce::jlimit (0.0f, 1.0f, (out + 60.0f) / 60.0f) * plot.getHeight();
                }
                else
                {
                    const float in = -1.5f + 3.0f * i / n;
                    const float k = 0.25f * dsp::dbToGain (state[pSatDrive]);
                    const float shaped = dsp::Saturator::shape ((int) state[pSatType], in, k);
                    const float mix = state[pSatMix] * 0.01f;
                    const float out = in + (shaped - in) * mix;
                    x = plot.getX() + (in + 1.5f) / 3.0f * plot.getWidth();
                    y = plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, out / 1.5f) * plot.getHeight() * 0.5f;
                }

                if (i == 0) curve.startNewSubPath (x, y);
                else        curve.lineTo (x, y);
            }

            const bool on = mode == Mode::Compressor ? state[pCompOn] >= 0.5f : state[pSatOn] >= 0.5f;
            g.setColour ((on ? Palette::accent : Palette::textFaint));
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            if (mode == Mode::Compressor)
            {
                const float tx = plot.getX() + (state[pCompThresh] + 60.0f) / 60.0f * plot.getWidth();
                g.setColour (Palette::amber.withAlpha (0.6f));
                g.drawVerticalLine ((int) tx, plot.getY(), plot.getBottom());
            }

            g.setColour (Palette::textFaint);
            g.setFont (font (10.0f));
            g.drawText (mode == Mode::Compressor ? "IN dB -> OUT dB" : "WAVESHAPE", r.reduced (10, 6), juce::Justification::topLeft);
            if (mode == Mode::Compressor && on)
            {
                g.setColour (Palette::amber);
                g.drawText ("GR " + juce::String (gr, 1) + " dB", r.reduced (10, 6), juce::Justification::bottomRight);
            }
        }

    private:
        Mode mode;
        ParamState state {};
        float gr = 0.0f;
    };

    //--------------------------------------------------------------------------
    class DynamicsPage : public ModuleRack::PageBase
    {
    public:
        DynamicsPage (Prizmv1AudioProcessor& p)
            : PageBase ("Dynamics (bus compressor)", "Glues the mix and controls peaks. Threshold sets where it starts; ratio sets how strongly."),
              thr (p.apvts, pCompThresh, "THRESHOLD"), ratio (p.apvts, pCompRatio, "RATIO"), attack (p.apvts, pCompAttack, "ATTACK"),
              release (p.apvts, pCompRelease, "RELEASE"), knee (p.apvts, pCompKnee, "KNEE"), makeup (p.apvts, pCompMakeup, "MAKEUP"),
              mix (p.apvts, pCompMix, "MIX"), hpf (p.apvts, pCompScHpf, "DETECTOR HPF")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &thr, &ratio, &attack, &release, &knee, &makeup, &mix, &hpf, &curve })
                addAndMakeVisible (c);
        }

        void update (const ParamState& p, const MeterSnapshot& m) override { curve.setState (p, m.compGrDb); }

        void resized() override
        {
            auto r = content();
            curve.setBounds (r.removeFromRight (juce::jmin (r.getHeight(), r.getWidth() / 3)).reduced (4));
            layoutGrid (r, { &thr, &ratio, &attack, &release, &knee, &makeup, &mix, &hpf }, 4);
        }

    private:
        Knob thr, ratio, attack, release, knee, makeup, mix, hpf;
        CurveView curve { CurveView::Mode::Compressor };
    };

    //--------------------------------------------------------------------------
    class SaturationPage : public ModuleRack::PageBase
    {
    public:
        SaturationPage (Prizmv1AudioProcessor& p)
            : PageBase ("Saturation (4x oversampled)", "Adds harmonics for warmth and density. Tape = smooth, Tube = rich even harmonics, Soft Clip = firmer."),
              type (p.apvts, pSatType), drive (p.apvts, pSatDrive, "DRIVE"), mix (p.apvts, pSatMix, "MIX")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &type, &drive, &mix, &curve })
                addAndMakeVisible (c);
        }

        void update (const ParamState& p, const MeterSnapshot& m) override { curve.setState (p, m.compGrDb); }

        void resized() override
        {
            auto r = content();
            curve.setBounds (r.removeFromRight (juce::jmin (r.getHeight(), r.getWidth() / 3)).reduced (4));
            auto left = r.removeFromLeft (juce::jmin (r.getWidth(), 360));
            type.setBounds (left.removeFromTop (28).withSizeKeepingCentre (160, 26));
            left.removeFromTop (6);
            layoutGrid (left, { &drive, &mix }, 2);
        }

    private:
        ParamCombo type;
        Knob drive, mix;
        CurveView curve { CurveView::Mode::Saturation };
    };

    //--------------------------------------------------------------------------
    class ImagerPage : public ModuleRack::PageBase
    {
    public:
        ImagerPage (Prizmv1AudioProcessor& p)
            : PageBase ("Stereo Imager", "Width scales the side signal (100% = unchanged). Mono bass keeps the low end centred and phase-safe."),
              width (p.apvts, pImgWidth, "WIDTH"), monoOn (p.apvts, pImgMonoOn, "Mono bass"), monoFreq (p.apvts, pImgMonoFreq, "MONO BELOW")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &width, &monoOn, &monoFreq })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto r = content();
            auto left = r.removeFromLeft (juce::jmin (r.getWidth(), 380));
            monoOn.setBounds (left.removeFromTop (26).withSizeKeepingCentre (130, 24));
            layoutGrid (left, { &width, &monoFreq }, 2);
        }

        void paintExtra (juce::Graphics& g) override
        {
            auto r = content().withTrimmedLeft (400);
            g.setColour (Palette::textDim);
            g.setFont (font (12.0f));
            g.drawFittedText ("Watch the vectorscope and correlation meter: keep correlation mostly above 0. "
                              "Use the Mono button in the header to check that nothing disappears in mono.",
                              r.removeFromTop (70), juce::Justification::topLeft, 4);
        }

    private:
        Knob width;
        ParamToggle monoOn;
        Knob monoFreq;
    };

    //--------------------------------------------------------------------------
    class LimiterPage : public ModuleRack::PageBase
    {
    public:
        LimiterPage (Prizmv1AudioProcessor& p)
            : PageBase ("Limiter (true-peak maximizer)", "Gain pushes the track into the limiter for loudness; the ceiling is the maximum output level."),
              gain (p.apvts, pLimGain, "GAIN"), ceiling (p.apvts, pLimCeiling, "CEILING"), release (p.apvts, pLimRelease, "RELEASE"),
              truePeak (p.apvts, pLimTruePeak, "True peak (inter-sample)")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &gain, &ceiling, &release, &truePeak })
                addAndMakeVisible (c);
        }

        void update (const ParamState& p, const MeterSnapshot& m) override
        {
            limiting = p[pLimOn] >= 0.5f ? m.limGrDb : 0.0f;
            repaint();
        }

        void resized() override
        {
            auto r = content();
            auto left = r.removeFromLeft (juce::jmin (r.getWidth(), 420));
            truePeak.setBounds (left.removeFromTop (26).withSizeKeepingCentre (220, 24));
            layoutGrid (left, { &gain, &ceiling, &release }, 3);
        }

        void paintExtra (juce::Graphics& g) override
        {
            auto r = content().withTrimmedLeft (440);
            g.setColour (limiting > 6.0f ? Palette::amber : Palette::textDim);
            g.setFont (font (13.0f, true));
            g.drawText ("Gain reduction now: " + juce::String (limiting, 1) + " dB", r.removeFromTop (20), juce::Justification::centredLeft);
            g.setColour (Palette::textDim);
            g.setFont (font (12.0f));
            g.drawFittedText ("2 ms lookahead. More than ~4-6 dB of constant gain reduction starts to flatten transients - "
                              "ask the assistant for a loudness target instead of guessing.",
                              r.removeFromTop (60), juce::Justification::topLeft, 4);
        }

    private:
        Knob gain, ceiling, release;
        ParamToggle truePeak;
        float limiting = 0.0f;
    };
}

//==============================================================================
ModuleRack::ModuleRack (Prizmv1AudioProcessor& p) : processor (p)
{
    auto& s = p.apvts;
    pills.add (new Pill (s, "Input", -1, Page::Gain));
    pills.add (new Pill (s, "EQ", pEqOn, Page::EQ));
    pills.add (new Pill (s, "Dynamics", pCompOn, Page::Dynamics));
    pills.add (new Pill (s, "Saturation", pSatOn, Page::Saturation));
    pills.add (new Pill (s, "Imager", pImgOn, Page::Imager));
    pills.add (new Pill (s, "Limiter", pLimOn, Page::Limiter));
    pills.add (new Pill (s, "Output", -1, Page::Gain));

    for (auto* pill : pills)
    {
        pill->onClick = [this] (Page page) { showPage (page); };
        addAndMakeVisible (pill);
    }

    pages.add (new GainPage (p));
    pages.add (new EQPage (p));
    pages.add (new DynamicsPage (p));
    pages.add (new SaturationPage (p));
    pages.add (new ImagerPage (p));
    pages.add (new LimiterPage (p));

    for (auto* page : pages)
        addChildComponent (page);

    showPage (Page::EQ);
}

ModuleRack::~ModuleRack() = default;

void ModuleRack::showPage (Page page)
{
    current = page;
    for (int i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (i == (int) page);
    for (auto* pill : pills)
        pill->setSelected (pill->page == page);
}

void ModuleRack::update (const MeterSnapshot& m)
{
    const auto p = processor.getParamState();

    int bands = 0;
    for (int g : { pEqLsGain, pEqP1Gain, pEqP2Gain, pEqP3Gain, pEqHsGain })
        bands += std::abs (p[(size_t) g]) >= 0.1f ? 1 : 0;
    bands += isOn (p, pEqHpfOn) ? 1 : 0;
    bands += isOn (p, pEqLpfOn) ? 1 : 0;

    pills[0]->setStatus (formatParamValue (pInGain, p[pInGain]), true);
    pills[1]->setStatus (! isOn (p, pEqOn) ? "Off" : (bands == 0 ? "Flat" : juce::String (bands) + (bands == 1 ? " band" : " bands")), isOn (p, pEqOn));
    pills[2]->setStatus (isOn (p, pCompOn) ? "GR " + juce::String (m.compGrDb, 1) + " dB" : juce::String ("Off"), isOn (p, pCompOn));
    pills[3]->setStatus (isOn (p, pSatOn) ? formatParamValue (pSatType, p[pSatType]) + " " + formatParamValue (pSatDrive, p[pSatDrive]).substr (1)
                                          : std::string ("Off"), isOn (p, pSatOn));
    pills[4]->setStatus (isOn (p, pImgOn) ? "Width " + formatParamValue (pImgWidth, p[pImgWidth]) + (isOn (p, pImgMonoOn) ? " + mono bass" : "")
                                          : std::string ("Off"), isOn (p, pImgOn));
    pills[5]->setStatus (isOn (p, pLimOn) ? "GR " + juce::String (m.limGrDb, 1) + " | " + juce::String (p[pLimCeiling], 1) + " dBTP"
                                          : juce::String ("Off"), isOn (p, pLimOn));
    pills[6]->setStatus (formatParamValue (pOutGain, p[pOutGain]), true);

    pages[(int) current]->update (p, m);
}

void ModuleRack::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (stripHeight + 8);
    drawPanel (g, r.toFloat());

    // Arrows between chain stages
    g.setColour (Palette::textFaint);
    for (int i = 0; i < pills.size() - 1; ++i)
    {
        const auto a = pills[i]->getBounds(), b = pills[i + 1]->getBounds();
        const float x0 = (float) a.getRight() + 1.0f, x1 = (float) b.getX() - 1.0f, y = (float) a.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (x0, y);
        arrow.lineTo (x1 - 1.0f, y);
        arrow.startNewSubPath (x1 - 4.0f, y - 3.0f);
        arrow.lineTo (x1 - 1.0f, y);
        arrow.lineTo (x1 - 4.0f, y + 3.0f);
        g.strokePath (arrow, juce::PathStrokeType (1.2f));
    }
}

void ModuleRack::resized()
{
    auto r = getLocalBounds();
    auto strip = r.removeFromTop (stripHeight);
    const int gap = 12;
    const int inOutW = juce::jmax (70, strip.getWidth() / 12);
    const int moduleW = (strip.getWidth() - 2 * inOutW - 6 * gap) / 5;

    pills[0]->setBounds (strip.removeFromLeft (inOutW));
    for (int i = 1; i <= 5; ++i)
    {
        strip.removeFromLeft (gap);
        pills[i]->setBounds (strip.removeFromLeft (moduleW));
    }
    strip.removeFromLeft (gap);
    pills[6]->setBounds (strip);

    r.removeFromTop (8);
    for (auto* page : pages)
        page->setBounds (r);
}

} // namespace prizm::ui
