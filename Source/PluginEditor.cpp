/*
  ==============================================================================

    PRIZM V2 - plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prizm::ui;

//==============================================================================
Prizmv1AudioProcessorEditor::Prizmv1AudioProcessorEditor (Prizmv1AudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      spectrum (p),
      rack (p),
      referencePanel (p),
      chat (p.getChat())
{
    setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &spectrum, &rack, &meters, &chat })
        addAndMakeVisible (c);
    addChildComponent (referencePanel);

    // Tabs for the lower-left area
    for (auto* b : { &tabChain, &tabReference })
    {
        setStyle (*b, "segment");
        addAndMakeVisible (b);
    }
    tabChain.onClick = [this] { showTab (false); };
    tabReference.onClick = [this] { showTab (true); };

    // Header: A/B monitoring
    setStyle (originalButton, "segment");
    setStyle (processedButton, "segment");
    setStyle (levelMatchButton, "ghost");
    setStyle (monoButton, "ghost");
    originalButton.setTooltip ("Hear the unprocessed input (delay-compensated) to compare");
    processedButton.setTooltip ("Hear PRIZM's processed output");
    levelMatchButton.setTooltip ("When comparing, play the original at the same loudness as the processed sound so you judge tone, not volume");
    monoButton.setTooltip ("Monitor in mono to check phase / mono compatibility (does not affect the saved sound)");
    originalButton.onClick = [this] { audioProcessor.setCompareOriginal (true); updateHeaderButtons(); };
    processedButton.onClick = [this] { audioProcessor.setCompareOriginal (false); updateHeaderButtons(); };
    levelMatchButton.onClick = [this] { audioProcessor.setLevelMatch (! audioProcessor.isLevelMatch()); updateHeaderButtons(); };
    monoButton.onClick = [this] { audioProcessor.setMonoCheck (! audioProcessor.isMonoCheck()); updateHeaderButtons(); };

    // Header: history
    for (auto* b : { &undoButton, &redoButton, &historyButton })
        setStyle (*b, "ghost");
    undoButton.onClick = [this] { audioProcessor.getHistory().undo(); };
    redoButton.onClick = [this] { audioProcessor.getHistory().redo(); };
    historyButton.onClick = [this] { showHistoryMenu(); };
    historyButton.setTooltip ("Restore any earlier state, or the original settings");

    for (auto* b : { &originalButton, &processedButton, &levelMatchButton, &monoButton, &undoButton, &redoButton, &historyButton })
        addAndMakeVisible (b);

    // Wiring
    audioProcessor.getHistory().addChangeListener (this);
    audioProcessor.getChat().onRequestReferenceFile = [this] { openReferenceChooser(); };
    referencePanel.onLoadReference = [this] { openReferenceChooser(); };
    referencePanel.onSendChat = [this] (const juce::String& text) { chat.sendText (text); };
    meters.loudness.onReset = [this] { audioProcessor.resetLoudnessMeters(); };

    fifoScratch.resize (1 << 16);
    showTab (false);
    updateHeaderButtons();

    setResizable (true, true);
    setResizeLimits (1080, 720, 2200, 1500);
    setSize (1240, 820);
    startTimerHz (30);
}

Prizmv1AudioProcessorEditor::~Prizmv1AudioProcessorEditor()
{
    stopTimer();
    audioProcessor.getHistory().removeChangeListener (this);
    audioProcessor.getChat().onRequestReferenceFile = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void Prizmv1AudioProcessorEditor::showTab (bool reference)
{
    referenceTab = reference;
    tabChain.setToggleState (! reference, juce::dontSendNotification);
    tabReference.setToggleState (reference, juce::dontSendNotification);
    rack.setVisible (! reference);
    referencePanel.setVisible (reference);
}

void Prizmv1AudioProcessorEditor::updateHeaderButtons()
{
    const bool original = audioProcessor.isComparingOriginal();
    originalButton.setToggleState (original, juce::dontSendNotification);
    processedButton.setToggleState (! original, juce::dontSendNotification);
    levelMatchButton.setToggleState (audioProcessor.isLevelMatch(), juce::dontSendNotification);
    monoButton.setToggleState (audioProcessor.isMonoCheck(), juce::dontSendNotification);

    auto& h = audioProcessor.getHistory();
    undoButton.setEnabled (h.canUndo());
    redoButton.setEnabled (h.canRedo());
    undoButton.setTooltip (h.canUndo() ? "Undo: " + h.getUndoLabel() : "Nothing to undo");
    redoButton.setTooltip (h.canRedo() ? "Redo: " + h.getRedoLabel() : "Nothing to redo");
}

void Prizmv1AudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateHeaderButtons();
}

void Prizmv1AudioProcessorEditor::openReferenceChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File(),
                                                   "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  audioProcessor.getChat().loadReference (file);
                                  showTab (true);
                              }
                          });
}

void Prizmv1AudioProcessorEditor::showHistoryMenu()
{
    auto& h = audioProcessor.getHistory();
    juce::PopupMenu menu;
    menu.addItem (1, "Undo" + (h.canUndo() ? ": " + h.getUndoLabel() : juce::String()), h.canUndo());
    menu.addItem (2, "Redo" + (h.canRedo() ? ": " + h.getRedoLabel() : juce::String()), h.canRedo());
    menu.addSeparator();
    menu.addItem (3, "Restore original settings (when this session was opened)");
    menu.addItem (4, "Reset loudness meters");

    const auto& entries = h.getUndoEntries();
    if (! entries.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Restore the state before...");
        int shown = 0;
        for (auto it = entries.rbegin(); it != entries.rend() && shown < 15; ++it, ++shown)
            menu.addItem (1000 + it->id, it->label + "   (" + it->time.formatted ("%H:%M:%S") + ")");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (historyButton),
                        [this] (int result)
                        {
                            auto& hist = audioProcessor.getHistory();
                            if (result == 1)       hist.undo();
                            else if (result == 2)  hist.redo();
                            else if (result == 3)  hist.restoreSessionStart();
                            else if (result == 4)  audioProcessor.resetLoudnessMeters();
                            else if (result >= 1000) hist.restoreBefore (result - 1000);
                        });
}

//==============================================================================
void Prizmv1AudioProcessorEditor::timerCallback()
{
    const auto m = audioProcessor.getMeters();
    const auto params = audioProcessor.getParamState();

    meters.update (m, audioProcessor.getTargetLufs(), audioProcessor.getCeilingDb(),
                   prizm::isOn (params, prizm::pCompOn), prizm::isOn (params, prizm::pLimOn), audioProcessor.isComparingOriginal());
    rack.update (m);

    // Spectrum: drain the lock-free FIFO filled by the audio thread.
    auto& specFifo = audioProcessor.getSpectrumFifo();
    for (int n; (n = specFifo.pop (fifoScratch.data(), (int) fifoScratch.size() / 2)) > 0;)
        spectrum.pushSamples (fifoScratch.data(), n);
    spectrum.update (m.audioActive);

    auto& scopeFifo = audioProcessor.getScopeFifo();
    for (int n; (n = scopeFifo.pop (fifoScratch.data(), (int) fifoScratch.size() / 2)) > 0;)
        meters.pushScopeSamples (fifoScratch.data(), n);

    if (audioProcessor.getReferenceVersion() != lastReferenceVersion)
    {
        lastReferenceVersion = audioProcessor.getReferenceVersion();
        const auto ref = audioProcessor.getReference();
        spectrum.setReference (ref.valid ? &ref.analysis : nullptr);
    }

    referencePanel.update();

    // The chat can toggle A/B and mono too - keep the header in sync.
    if (originalButton.getToggleState() != audioProcessor.isComparingOriginal()
        || monoButton.getToggleState() != audioProcessor.isMonoCheck())
        updateHeaderButtons();

    repaint (footerArea);
}

//==============================================================================
void Prizmv1AudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // V1 background gradient
    juce::ColourGradient bgGrad (juce::Colour (0xff2a2a2a), 0, 0, juce::Colour (0xff151515), 0, (float) bounds.getHeight(), false);
    g.setGradientFill (bgGrad);
    g.fillRect (bounds);

    // V1 header: green PRIZM + "AI Audio Assistant"
    auto topBar = headerArea;
    const auto prizmFont = font (26.0f, true);
    g.setColour (Palette::accent);
    g.setFont (prizmFont);
    g.drawText ("PRIZM", topBar.withTrimmedLeft (20), juce::Justification::centredLeft);

    g.setColour (Palette::textDim);
    g.setFont (font (14.0f));
    g.drawText ("AI Audio Assistant", topBar.withTrimmedLeft (114).translated (0, 2), juce::Justification::centredLeft);

    auto v2 = juce::Rectangle<float> (250.0f, (float) topBar.getCentreY() - 9.0f, 30.0f, 18.0f);
    g.setColour (Palette::accent.withAlpha (0.15f));
    g.fillRoundedRectangle (v2, 9.0f);
    g.setColour (Palette::accent);
    g.setFont (font (11.0f, true));
    g.drawText ("V2", v2, juce::Justification::centred);

    g.setColour (Palette::textFaint);
    g.setFont (font (10.5f, true));
    g.drawText ("MONITOR", originalButton.getBounds().translated (-68, 0).withWidth (62), juce::Justification::centredRight);

    g.setColour (juce::Colour (0xff333333));
    g.drawHorizontalLine (topBar.getBottom(), 0.0f, (float) getWidth());

    // Spectrum title (V1 label)
    drawSectionTitle (g, spectrumTitle, "Spectrum Analyzer", "20 Hz - 20 kHz  |  input, output, reference & EQ");

    // Footer (V1 "PrizmPrototype")
    g.setColour (Palette::textFaint);
    g.setFont (font (11.0f));
    const double fs = audioProcessor.getCurrentSampleRate();
    const int lat = audioProcessor.getChainLatencySamples();
    g.drawText (juce::String (fs / 1000.0, 1) + " kHz  |  latency " + juce::String (1000.0 * lat / fs, 1) + " ms ("
                    + juce::String (lat) + " samples)",
                footerArea.withTrimmedLeft (16), juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (font (12.0f));
    g.drawText ("PrizmPrototype V2", footerArea, juce::Justification::centred);

    if (audioProcessor.isComparingOriginal())
    {
        g.setColour (Palette::amber);
        g.setFont (font (11.0f, true));
        g.drawText ("LISTENING TO ORIGINAL", footerArea.withTrimmedRight (16), juce::Justification::centredRight);
    }
    else if (audioProcessor.isMonoCheck())
    {
        g.setColour (Palette::amber);
        g.setFont (font (11.0f, true));
        g.drawText ("MONO CHECK ON", footerArea.withTrimmedRight (16), juce::Justification::centredRight);
    }
}

void Prizmv1AudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    headerArea = bounds.removeFromTop (56);
    footerArea = bounds.removeFromBottom (26);

    // Header controls (right side)
    {
        auto h = headerArea.reduced (14, 12);
        historyButton.setBounds (h.removeFromRight (78));
        h.removeFromRight (6);
        redoButton.setBounds (h.removeFromRight (62));
        h.removeFromRight (6);
        undoButton.setBounds (h.removeFromRight (62));
        h.removeFromRight (22);
        monoButton.setBounds (h.removeFromRight (64));
        h.removeFromRight (6);
        levelMatchButton.setBounds (h.removeFromRight (100));
        h.removeFromRight (10);
        processedButton.setBounds (h.removeFromRight (92));
        originalButton.setBounds (h.removeFromRight (84));
    }

    auto body = bounds.reduced (14, 10);
    const int gap = 12;
    const int rightW = juce::jlimit (380, 520, (int) (body.getWidth() * 0.35f));
    auto right = body.removeFromRight (rightW);
    body.removeFromRight (gap);
    auto left = body;

    // Right column: meters, then chat
    const int metersH = juce::jlimit (270, 330, (int) (right.getHeight() * 0.4f));
    meters.setBounds (right.removeFromTop (metersH));
    right.removeFromTop (gap);
    chat.setBounds (right);

    // Left column: spectrum, tabs, chain or reference
    spectrumTitle = left.removeFromTop (20);
    const int spectrumH = juce::jlimit (200, 420, (int) (left.getHeight() * 0.42f));
    spectrum.setBounds (left.removeFromTop (spectrumH));
    left.removeFromTop (gap);

    auto tabs = left.removeFromTop (28);
    tabChain.setBounds (tabs.removeFromLeft (150));
    tabs.removeFromLeft (6);
    tabReference.setBounds (tabs.removeFromLeft (110));
    left.removeFromTop (8);

    lowerArea = left;
    rack.setBounds (left);
    referencePanel.setBounds (left);
}
