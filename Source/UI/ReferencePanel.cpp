/*
  ==============================================================================

    PRIZM V2 - reference track panel.

  ==============================================================================
*/

#include "ReferencePanel.h"
#include "../Assistant/MasteringAssistant.h"
#include "../PluginProcessor.h"

namespace prizm::ui
{

ReferencePanel::ReferencePanel (Prizmv1AudioProcessor& p) : processor (p)
{
    setStyle (loadButton, "primary");
    setStyle (clearButton, "ghost");
    setStyle (analyseButton, "ghost");
    setStyle (matchButton, "accent");
    setStyle (sourceInput, "segment");
    setStyle (sourceOutput, "segment");

    sourceInput.setClickingTogglesState (false);
    sourceOutput.setClickingTogglesState (false);
    sourceInput.setToggleState (true, juce::dontSendNotification);

    loadButton.onClick = [this] { if (onLoadReference) onLoadReference(); };
    clearButton.onClick = [this] { processor.clearReference(); };
    analyseButton.onClick = [this] { processor.getChat().requestTrackAnalysis (showOutput); };
    matchButton.onClick = [this] { if (onSendChat) onSendChat ("match the reference"); };
    sourceInput.onClick = [this] { showOutput = false; refresh(); };
    sourceOutput.onClick = [this] { showOutput = true; refresh(); };

    analyseButton.setTooltip ("Listens to the next few seconds of playback and measures your track");
    matchButton.setTooltip ("Ask the assistant for a chain that moves your track towards the reference (you approve every change)");

    for (auto* b : { &loadButton, &clearButton, &analyseButton, &matchButton, &sourceInput, &sourceOutput })
        addAndMakeVisible (b);
}

const dsp::AnalysisResult* ReferencePanel::trackAnalysis() const
{
    auto& chat = processor.getChat();
    return showOutput ? chat.getRecentOutputAnalysis() : chat.getRecentInputAnalysis();
}

void ReferencePanel::refresh()
{
    const auto info = processor.getReference();
    hasReference = info.valid;
    referenceName = info.name;
    reference = info.analysis;

    sourceInput.setToggleState (! showOutput, juce::dontSendNotification);
    sourceOutput.setToggleState (showOutput, juce::dontSendNotification);
    clearButton.setEnabled (hasReference);
    matchButton.setEnabled (hasReference);

    recommendations.clear();
    if (hasReference)
        if (auto* t = trackAnalysis())
            recommendations = assistant::referenceRecommendations (*t, reference);

    repaint();
}

void ReferencePanel::update()
{
    const int rv = processor.getReferenceVersion();
    const int cv = processor.getChat().getVersion();
    if (rv != lastReferenceVersion || cv != lastChatVersion)
    {
        lastReferenceVersion = rv;
        lastChatVersion = cv;
        refresh();
    }

    const auto st = processor.getWorker().getStatus();
    const juce::String newStatus = st.busy ? st.message : juce::String();
    if (newStatus != status || st.progress != progress)
    {
        status = newStatus;
        progress = st.progress;
        analyseButton.setEnabled (! st.busy);
        repaint (infoArea);
    }
}

void ReferencePanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    infoArea = r.removeFromLeft (juce::jmin (300, r.getWidth() * 2 / 5));
    r.removeFromLeft (16);

    auto info = infoArea;
    info.removeFromTop (22);
    auto row = info.removeFromTop (30);
    loadButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    clearButton.setBounds (row.removeFromLeft (70));
    info.removeFromTop (54);   // name + details text
    auto src = info.removeFromTop (26);
    sourceInput.setBounds (src.removeFromLeft (src.getWidth() / 2).reduced (0, 1));
    sourceOutput.setBounds (src.reduced (0, 1));
    info.removeFromTop (8);
    analyseButton.setBounds (info.removeFromTop (28));
    info.removeFromTop (8);
    matchButton.setBounds (info.removeFromTop (32));

    tableArea = r.removeFromTop (juce::jmin (150, r.getHeight() / 2));
    r.removeFromTop (6);
    barsArea = r.removeFromTop (juce::jmin (r.getHeight() / 2, 96));
    r.removeFromTop (6);
    recsArea = r;
}

void ReferencePanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());

    // Left: reference info
    {
        auto r = infoArea;
        drawSectionTitle (g, r.removeFromTop (18), "Reference track");
        r.removeFromTop (34);
        auto nameRow = r.removeFromTop (22);
        g.setColour (hasReference ? Palette::reference : Palette::textFaint);
        g.setFont (font (13.5f, true));
        g.drawFittedText (hasReference ? referenceName : juce::String ("No reference loaded"), nameRow, juce::Justification::centredLeft, 1);

        g.setColour (Palette::textDim);
        g.setFont (font (11.5f));
        if (hasReference)
            g.drawFittedText (juce::String::formatted ("%.0f s analysed - %.1f kHz - only analysed, never played",
                                                       reference.secondsAnalysed, reference.sampleRate / 1000.0),
                              r.removeFromTop (18), juce::Justification::centredLeft, 1);
        else
            g.drawFittedText ("WAV, AIFF, FLAC, OGG or MP3", r.removeFromTop (18), juce::Justification::centredLeft, 1);

        r.removeFromTop (14);
        g.setColour (Palette::textFaint);
        g.setFont (font (10.5f, true));
        g.drawText ("COMPARE AGAINST YOUR TRACK:", sourceInput.getBounds().translated (0, -16).withWidth (220), juce::Justification::centredLeft);

        if (status.isNotEmpty())
        {
            auto s = juce::Rectangle<int> (infoArea.getX(), matchButton.getBottom() + 8, infoArea.getWidth(), 34);
            g.setColour (Palette::amber);
            g.setFont (font (11.5f));
            g.drawFittedText (status, s.removeFromTop (28), juce::Justification::topLeft, 2);
            if (progress >= 0.0f)
            {
                auto bar = s.removeFromTop (4).toFloat();
                g.setColour (Palette::panelRaised);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (Palette::amber);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.0f);
            }
        }
    }

    const auto* track = trackAnalysis();

    if (! hasReference)
    {
        auto r = tableArea.getUnion (barsArea).getUnion (recsArea).reduced (8);
        g.setColour (Palette::textDim);
        g.setFont (font (13.0f));
        g.drawFittedText ("Load a commercial track whose sound you love.\n\n"
                          "PRIZM measures its loudness, tonal balance, dynamics and stereo width, shows how your track compares, "
                          "and can propose a mastering chain to close the gap - you approve every change.\n\n"
                          "The reference is only analysed in the background. It is never played through or mixed into your audio.",
                          r, juce::Justification::centredLeft, 12);
        return;
    }

    // Comparison table
    {
        auto r = tableArea;
        const int colW = (r.getWidth() - 150) / 3;
        auto header = r.removeFromTop (16);
        g.setFont (font (10.5f, true));
        g.setColour (Palette::textFaint);
        header.removeFromLeft (150);
        g.drawText (showOutput ? "PROCESSED" : "UNPROCESSED", header.removeFromLeft (colW), juce::Justification::centredRight);
        g.drawText ("REFERENCE", header.removeFromLeft (colW), juce::Justification::centredRight);
        g.drawText ("DIFFERENCE", header, juce::Justification::centredRight);

        struct Row { const char* name; float yours, ref; const char* unit; int decimals; };
        const auto& t = track != nullptr ? *track : reference;
        const Row rows[] = {
            { "Loudness (integrated)", t.integratedLufs, reference.integratedLufs, "LUFS", 1 },
            { "True peak", t.truePeakDb, reference.truePeakDb, "dBTP", 1 },
            { "Peak-to-loudness (PLR)", t.plrDb, reference.plrDb, "dB", 1 },
            { "Loudness range", t.loudnessRange, reference.loudnessRange, "LU", 1 },
            { "Stereo width (side/mid)", t.sideToMidDb, reference.sideToMidDb, "dB", 1 },
            { "Correlation", t.correlation, reference.correlation, "", 2 },
            { "Tonal tilt", t.tiltDbPerOctave, reference.tiltDbPerOctave, "dB/oct", 1 },
        };

        const int rowH = juce::jmax (16, r.getHeight() / 7);
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (rowH);
            g.setColour (Palette::textDim);
            g.setFont (font (12.0f));
            g.drawText (row.name, line.removeFromLeft (150), juce::Justification::centredLeft);

            g.setFont (font (12.0f, true));
            g.setColour (track != nullptr ? Palette::text : Palette::textFaint);
            g.drawText (track != nullptr ? juce::String (row.yours, row.decimals) + " " + row.unit : juce::String ("--"),
                        line.removeFromLeft (colW), juce::Justification::centredRight);
            g.setColour (Palette::reference);
            g.drawText (juce::String (row.ref, row.decimals) + " " + row.unit, line.removeFromLeft (colW), juce::Justification::centredRight);

            if (track != nullptr)
            {
                const float d = row.yours - row.ref;
                g.setColour (std::abs (d) < (row.decimals == 2 ? 0.1f : 1.0f) ? Palette::accent : Palette::amber);
                g.drawText ((d >= 0 ? "+" : "") + juce::String (d, row.decimals), line, juce::Justification::centredRight);
            }
        }
    }

    // Tonal balance bars (difference per band, +/-6 dB)
    {
        auto r = barsArea;
        drawSectionTitle (g, r.removeFromTop (16), "Tonal balance vs reference", track != nullptr ? "" : "analyze your track to compare");
        const int n = dsp::AnalysisResult::numBands;
        const int w = r.getWidth() / n;
        for (int b = 0; b < n; ++b)
        {
            auto cell = juce::Rectangle<int> (r.getX() + b * w, r.getY(), w, r.getHeight()).reduced (4, 0);
            auto label = cell.removeFromBottom (14);
            auto valueRow = cell.removeFromBottom (14);
            auto bar = cell.toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Palette::grid);
            g.drawHorizontalLine ((int) bar.getCentreY(), bar.getX(), bar.getRight());

            if (track != nullptr)
            {
                const float d = juce::jlimit (-6.0f, 6.0f, track->bandDb[(size_t) b] - reference.bandDb[(size_t) b]);
                const float h = std::abs (d) / 6.0f * bar.getHeight() * 0.5f;
                g.setColour (std::abs (d) < 1.5f ? Palette::accent : Palette::amber);
                g.fillRoundedRectangle (bar.getX() + 4.0f, d > 0 ? bar.getCentreY() - h : bar.getCentreY(), bar.getWidth() - 8.0f, h, 2.0f);
                g.setFont (font (10.5f));
                g.setColour (Palette::text);
                g.drawText ((d >= 0 ? "+" : "") + juce::String (d, 1), valueRow, juce::Justification::centred);
            }

            g.setColour (Palette::textDim);
            g.setFont (font (10.5f, true));
            g.drawText (dsp::AnalysisResult::bandName (b), label, juce::Justification::centred);
        }
    }

    // Recommendations
    {
        auto r = recsArea;
        drawSectionTitle (g, r.removeFromTop (16), "What to change");
        g.setFont (font (12.0f));
        if (track == nullptr)
        {
            g.setColour (Palette::textFaint);
            g.drawFittedText ("Press play and click \"Analyze my track\" to get recommendations.", r, juce::Justification::topLeft, 2);
            return;
        }

        for (auto& line : recommendations)
        {
            const bool twoLines = juce::GlyphArrangement::getStringWidth (font (12.0f), line) > (float) (r.getWidth() - 14);
            const int h = twoLines ? 31 : 17;
            if (r.getHeight() < h)
                break;
            auto row = r.removeFromTop (h);
            g.setColour (Palette::accent);
            g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getY() + 6.5f, 4.0f, 4.0f);
            g.setColour (Palette::text);
            g.drawFittedText (line, row.withTrimmedLeft (12), juce::Justification::topLeft, twoLines ? 2 : 1, 1.0f);
        }
    }
}

} // namespace prizm::ui
