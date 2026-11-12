/*
  ==============================================================================

    PRIZM V2 - redesigned AI chat panel.

  ==============================================================================
*/

#include "ChatPanel.h"

namespace prizm::ui
{

namespace
{
    const juce::String bulletGlyph = juce::String::fromUTF8 ("\xe2\x80\xa2");
    const juce::String arrowGlyph  = juce::String::fromUTF8 ("\xe2\x86\x92");

    constexpr int avatarWidth = 32;
    constexpr int maxCollapsedRows = 6;

    /** Markdown-lite: "**bold**" and lines starting with "- " as bullets. */
    juce::AttributedString richText (const juce::String& text, float size, juce::Colour colour, juce::Colour boldColour)
    {
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::topLeft);
        as.setLineSpacing (3.0f);

        const auto lines = juce::StringArray::fromLines (text);
        for (int li = 0; li < lines.size(); ++li)
        {
            juce::String line = lines[li];
            if (line.startsWith ("- "))
            {
                as.append ("  " + bulletGlyph + "  ", font (size), boldColour);
                line = line.substring (2);
            }

            bool bold = false;
            int pos = 0;
            while (pos <= line.length())
            {
                const int next = line.indexOf (pos, "**");
                const auto seg = next < 0 ? line.substring (pos) : line.substring (pos, next);
                if (seg.isNotEmpty())
                    as.append (seg, font (size, bold), bold ? boldColour : colour);
                if (next < 0)
                    break;
                bold = ! bold;
                pos = next + 2;
            }

            if (li < lines.size() - 1)
                as.append ("\n", font (size), colour);
        }

        return as;
    }

    juce::TextLayout layoutText (const juce::AttributedString& as, float width)
    {
        juce::TextLayout l;
        l.createLayout (as, juce::jmax (40.0f, width));
        return l;
    }
}

//==============================================================================
class ChatPanel::MessageItem : public juce::Component
{
public:
    MessageItem (ChatSession& s, int id) : session (s), messageId (id)
    {
        setStyle (applyButton, "accent");
        setStyle (dismissButton, "ghost");
        setStyle (undoButton, "ghost");
        setStyle (moreButton, "chip");

        applyButton.onClick = [this] { session.applyProposal (messageId); };
        dismissButton.onClick = [this] { session.dismissProposal (messageId); };
        undoButton.onClick = [this] { session.undoProposal (messageId); };
        moreButton.onClick = [this]
        {
            expanded = ! expanded;
            rebuildControls();
            if (onLayoutChanged)
                onLayoutChanged();
        };

        for (auto* b : { &applyButton, &dismissButton, &undoButton, &moreButton })
            addChildComponent (b);
    }

    int getMessageId() const noexcept { return messageId; }
    bool isStatus() const noexcept    { return msg.role == ChatMessage::Role::Status; }

    /** Returns true if anything visible changed. */
    bool setMessage (const ChatMessage& m)
    {
        juce::String sig;
        sig << (int) m.role << "|" << m.text << "|" << (int) m.cardState << "|" << m.progress << "|" << (int) expanded;
        if (m.proposal)
            for (auto& c : m.proposal->changes)
                sig << (c.selected ? "1" : "0");

        if (sig == signature)
            return false;

        signature = sig;
        msg = m;
        rebuildControls();
        return true;
    }

    int layoutForWidth (int w)
    {
        width = w;
        rows.clear();
        notes.clear();

        if (msg.role == ChatMessage::Role::Status)
        {
            textLayout = layoutText (richText (msg.text, 12.5f, Palette::amber, Palette::amber), (float) (w - avatarWidth - 40));
            bubble = { (float) avatarWidth, 0.0f, (float) (w - avatarWidth - 8), textLayout.getHeight() + (msg.progress >= 0.0f ? 26.0f : 18.0f) };
            return (int) bubble.getBottom() + 8;
        }

        const bool user = msg.role == ChatMessage::Role::User;
        const float maxW = user ? w * 0.78f : (float) (w - avatarWidth - 12);
        textLayout = layoutText (richText (msg.text, 13.5f, user ? juce::Colours::white : juce::Colour (0xffdddddd),
                                           user ? juce::Colours::white : Palette::accentSoft),
                                 maxW - 24.0f);

        const float bw = juce::jmin (maxW, textLayout.getWidth() + 26.0f);
        const float bh = textLayout.getHeight() + 18.0f;
        bubble = user ? juce::Rectangle<float> (w - bw - 2.0f, 0.0f, bw, bh)
                      : juce::Rectangle<float> ((float) avatarWidth, 0.0f, bw, bh);

        int bottom = (int) bubble.getBottom();

        if (msg.proposal)
            bottom = layoutCard (bottom + 8);

        return bottom + 10;
    }

    void paint (juce::Graphics& g) override
    {
        if (msg.role == ChatMessage::Role::Status)
        {
            paintAvatar (g, true);
            g.setColour (Palette::amber.withAlpha (0.08f));
            g.fillRoundedRectangle (bubble, 10.0f);
            g.setColour (Palette::amber.withAlpha (0.35f));
            g.drawRoundedRectangle (bubble.reduced (0.5f), 10.0f, 1.0f);

            // Animated "working" dots
            const int phase = (int) (juce::Time::getMillisecondCounter() / 300) % 3;
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (Palette::amber.withAlpha (i == phase ? 1.0f : 0.3f));
                g.fillEllipse (bubble.getX() + 12.0f + i * 9.0f, bubble.getY() + 14.0f, 5.0f, 5.0f);
            }

            textLayout.draw (g, bubble.withTrimmedLeft (44.0f).withTrimmedTop (8.0f));

            if (msg.progress >= 0.0f)
            {
                auto bar = juce::Rectangle<float> (bubble.getX() + 12.0f, bubble.getBottom() - 12.0f, bubble.getWidth() - 24.0f, 4.0f);
                g.setColour (Palette::panelRaised);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (Palette::amber);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, msg.progress)), 2.0f);
            }
            return;
        }

        const bool user = msg.role == ChatMessage::Role::User;

        if (user)
        {
            g.setColour (Palette::blueBubble);
            g.fillRoundedRectangle (bubble, 12.0f);
        }
        else
        {
            paintAvatar (g, false);
            g.setColour (juce::Colour (0xff262626));
            g.fillRoundedRectangle (bubble, 12.0f);
            g.setColour (Palette::border);
            g.drawRoundedRectangle (bubble.reduced (0.5f), 12.0f, 1.0f);
        }

        textLayout.draw (g, bubble.reduced (13.0f, 9.0f));

        if (msg.proposal)
            paintCard (g);
    }

    void resized() override {}

    std::function<void()> onLayoutChanged;

private:
    struct Row
    {
        int changeIndex = -1;
        juce::String header;               // module name when a new group starts
        juce::Rectangle<int> headerRect, row;
        juce::TextLayout reason;
        juce::Rectangle<float> reasonRect;
    };

    struct Note
    {
        juce::TextLayout layout;
        juce::Rectangle<float> rect;
    };

    void paintAvatar (juce::Graphics& g, bool working)
    {
        auto a = juce::Rectangle<float> (2.0f, 2.0f, 24.0f, 24.0f);
        g.setColour ((working ? Palette::amber : Palette::accent).withAlpha (0.18f));
        g.fillEllipse (a);
        g.setColour (working ? Palette::amber : Palette::accent);
        g.drawEllipse (a.reduced (0.5f), 1.0f);
        g.setFont (font (12.0f, true));
        g.drawText ("P", a, juce::Justification::centred);
    }

    std::vector<int> visibleChanges() const
    {
        std::vector<int> idx;
        if (! msg.proposal)
            return idx;

        // Group by module so the card reads like the signal chain.
        for (int module = 0; module <= (int) Module::Limiter; ++module)
            for (int i = 0; i < (int) msg.proposal->changes.size(); ++i)
                if ((int) paramInfo (msg.proposal->changes[(size_t) i].param).module == module)
                    idx.push_back (i);

        if (! expanded && (int) idx.size() > maxCollapsedRows)
            idx.resize ((size_t) maxCollapsedRows);
        return idx;
    }

    void rebuildControls()
    {
        toggles.clear();
        const bool pending = msg.cardState == ChatMessage::CardState::Pending;

        if (msg.proposal)
        {
            for (int i = 0; i < (int) msg.proposal->changes.size(); ++i)
            {
                auto* t = toggles.add (new juce::ToggleButton());
                t->setToggleState (msg.proposal->changes[(size_t) i].selected, juce::dontSendNotification);
                t->setEnabled (pending);
                t->setTooltip ("Include this change");
                const int index = i;
                t->onClick = [this, t, index] { session.setChangeSelected (messageId, index, t->getToggleState()); };
                addChildComponent (t);
            }

            const int n = msg.proposal->numSelected();
            applyButton.setButtonText ("Apply " + juce::String (n) + (n == 1 ? " change" : " changes"));
            applyButton.setEnabled (n > 0);
            moreButton.setButtonText (expanded ? "Show fewer" : "Show all " + juce::String (msg.proposal->changes.size()) + " changes");
        }

        applyButton.setVisible (pending && msg.proposal.has_value());
        dismissButton.setVisible (pending && msg.proposal.has_value());
        undoButton.setVisible (msg.cardState == ChatMessage::CardState::Applied);
        moreButton.setVisible (msg.proposal && (int) msg.proposal->changes.size() > maxCollapsedRows);
    }

    int layoutCard (int top)
    {
        const auto& p = *msg.proposal;
        card = juce::Rectangle<int> (avatarWidth, top, width - avatarWidth - 4, 0);
        const int innerX = card.getX() + 12, innerW = card.getWidth() - 24;
        int y = top + 10;

        titleRect = { innerX, y, innerW, 20 };
        y += 24;

        if (p.prediction.valid)
        {
            predictionRect = { innerX, y, innerW, 22 };
            y += 28;
        }
        else
        {
            predictionRect = {};
        }

        for (auto* t : toggles)
            t->setVisible (false);

        Module lastModule = (Module) -1;
        for (int idx : visibleChanges())
        {
            const auto& c = p.changes[(size_t) idx];
            Row row;
            row.changeIndex = idx;

            const auto module = paramInfo (c.param).module;
            if (module != lastModule)
            {
                row.header = moduleName (module);
                row.headerRect = { innerX, y, innerW, 16 };
                y += 18;
                lastModule = module;
            }

            row.row = { innerX, y, innerW, 20 };
            toggles[idx]->setBounds (row.row.withWidth (22));
            toggles[idx]->setVisible (true);
            y += 20;

            if (! c.reason.empty())
            {
                row.reason = layoutText (richText (c.reason, 11.5f, Palette::textFaint, Palette::textDim), (float) (innerW - 28));
                row.reasonRect = juce::Rectangle<float> ((float) innerX + 28.0f, (float) y, (float) innerW - 28.0f, row.reason.getHeight());
                y += (int) row.reason.getHeight() + 4;
            }

            rows.push_back (std::move (row));
        }

        if (moreButton.isVisible())
        {
            moreButton.setBounds (innerX + 26, y + 2, 170, 22);
            y += 28;
        }

        for (auto& n : p.notes)
        {
            Note note;
            note.layout = layoutText (richText (n, 11.5f, Palette::amber, Palette::amber), (float) (innerW - 16));
            note.rect = { (float) innerX + 14.0f, (float) y + 2.0f, (float) innerW - 16.0f, note.layout.getHeight() };
            y += (int) note.layout.getHeight() + 6;
            notes.push_back (std::move (note));
        }

        y += 4;
        stateRect = {};

        if (applyButton.isVisible())
        {
            applyButton.setBounds (innerX, y, 150, 30);
            dismissButton.setBounds (innerX + 158, y, 90, 30);
            y += 38;
        }
        else if (msg.cardState != ChatMessage::CardState::Pending)
        {
            stateRect = { innerX, y, innerW, 26 };
            if (undoButton.isVisible())
                undoButton.setBounds (innerX + innerW - 80, y, 80, 26);
            y += 32;
        }

        card.setBottom (y);
        return card.getBottom();
    }

    void paintCard (juce::Graphics& g)
    {
        const auto& p = *msg.proposal;
        const auto state = msg.cardState;
        const auto accentCol = state == ChatMessage::CardState::Pending ? Palette::accent
                             : state == ChatMessage::CardState::Applied ? Palette::accent.withAlpha (0.6f)
                                                                        : Palette::textFaint;

        g.setColour (state == ChatMessage::CardState::Pending ? juce::Colour (0xff1b2621) : juce::Colour (0xff1f1f1f));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (accentCol.withAlpha (0.55f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (accentCol);
        g.fillRoundedRectangle (card.toFloat().withWidth (3.0f).reduced (0.0f, 8.0f), 1.5f);

        // Title + badge
        const juce::String badge = state == ChatMessage::CardState::Pending ? "AWAITING YOUR APPROVAL"
                                 : state == ChatMessage::CardState::Applied ? "APPLIED"
                                 : state == ChatMessage::CardState::Undone  ? "REVERTED" : "DISMISSED";
        g.setFont (font (10.0f, true));
        const int badgeW = juce::GlyphArrangement::getStringWidthInt (font (10.0f, true), badge) + 14;
        auto titleArea = titleRect;
        auto badgeRect = titleArea.removeFromRight (badgeW).withSizeKeepingCentre (badgeW, 18).toFloat();
        g.setColour ((state == ChatMessage::CardState::Pending ? Palette::amber : accentCol).withAlpha (0.15f));
        g.fillRoundedRectangle (badgeRect, 9.0f);
        g.setColour (state == ChatMessage::CardState::Pending ? Palette::amber : accentCol);
        g.drawText (badge, badgeRect, juce::Justification::centred);

        g.setColour (Palette::text);
        g.setFont (font (14.0f, true));
        g.drawFittedText (p.title, titleArea.withTrimmedRight (6), juce::Justification::centredLeft, 1);

        // Prediction chips
        if (! predictionRect.isEmpty())
        {
            auto r = predictionRect;
            auto chip = [&] (const juce::String& label, const juce::String& value)
            {
                const auto text = label + " " + value;
                const int w = juce::GlyphArrangement::getStringWidthInt (font (11.5f), text) + 18;
                auto c = r.removeFromLeft (w).toFloat();
                r.removeFromLeft (6);
                g.setColour (Palette::panelRaised);
                g.fillRoundedRectangle (c, 11.0f);
                g.setColour (Palette::textDim);
                g.setFont (font (11.5f));
                g.drawText (text, c, juce::Justification::centred);
            };

            g.setColour (Palette::textFaint);
            g.setFont (font (10.5f, true));
            g.drawText ("PREDICTED", r.removeFromLeft (70), juce::Justification::centredLeft);
            chip ("Loudness", juce::String (p.prediction.integratedLufs, 1) + " LUFS");
            chip ("True peak", juce::String (p.prediction.truePeakDb, 1) + " dBTP");
            chip ("Limiting", juce::String (p.prediction.avgLimiterGrDb, 1) + " dB");
        }

        // Changes
        for (auto& row : rows)
        {
            if (row.header.isNotEmpty())
            {
                g.setColour (Palette::textFaint);
                g.setFont (font (10.5f, true));
                g.drawText (row.header.toUpperCase(), row.headerRect, juce::Justification::centredLeft);
            }

            const auto& c = p.changes[(size_t) row.changeIndex];
            auto r = row.row.withTrimmedLeft (28);
            const auto values = juce::String (formatParamValue (c.param, c.from)) + "  " + arrowGlyph + "  " + formatParamValue (c.param, c.to);
            const int vw = juce::GlyphArrangement::getStringWidthInt (font (12.5f, true), values) + 4;

            g.setColour (c.selected ? Palette::text : Palette::textFaint);
            g.setFont (font (12.5f));
            g.drawFittedText (paramInfo (c.param).name, r.withTrimmedRight (vw + 6), juce::Justification::centredLeft, 1);
            g.setColour (c.selected ? Palette::accentSoft : Palette::textFaint);
            g.setFont (font (12.5f, true));
            g.drawText (values, r.removeFromRight (vw), juce::Justification::centredRight);

            if (! row.reasonRect.isEmpty())
                row.reason.draw (g, row.reasonRect);
        }

        for (auto& note : notes)
        {
            g.setColour (Palette::amber);
            g.fillEllipse (note.rect.getX() - 10.0f, note.rect.getY() + 5.0f, 5.0f, 5.0f);
            note.layout.draw (g, note.rect);
        }

        if (! stateRect.isEmpty())
        {
            g.setFont (font (12.0f, true));
            g.setColour (accentCol);
            const juce::String text = state == ChatMessage::CardState::Applied ? "Applied - compare with Original / Processed, or undo"
                                    : state == ChatMessage::CardState::Undone  ? "Reverted - your previous settings were restored"
                                                                               : "Dismissed - nothing was changed";
            g.drawFittedText (text, stateRect.withTrimmedRight (undoButton.isVisible() ? 90 : 0), juce::Justification::centredLeft, 1);
        }
    }

    ChatSession& session;
    const int messageId;
    ChatMessage msg;
    juce::String signature;
    bool expanded = false;
    int width = 300;

    juce::TextLayout textLayout;
    juce::Rectangle<float> bubble;
    juce::Rectangle<int> card, titleRect, predictionRect, stateRect;
    std::vector<Row> rows;
    std::vector<Note> notes;

    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::TextButton applyButton { "Apply" }, dismissButton { "Dismiss" }, undoButton { "Undo" }, moreButton { "Show all" };
};

//==============================================================================
class ChatPanel::MessageList : public juce::Component
{
public:
    explicit MessageList (ChatSession& s) : session (s) {}

    /** Brings the item list in line with the session. Returns true if layout changed. */
    bool sync (const std::vector<ChatMessage>& messages)
    {
        bool changed = false;

        // Remove items whose message is gone.
        for (int i = items.size(); --i >= 0;)
        {
            const int id = items[i]->getMessageId();
            const bool exists = std::any_of (messages.begin(), messages.end(), [id] (const ChatMessage& m) { return m.id == id; });
            if (! exists)
            {
                items.remove (i);
                changed = true;
            }
        }

        for (size_t i = 0; i < messages.size(); ++i)
        {
            const auto& m = messages[i];
            MessageItem* item = nullptr;
            for (auto* it : items)
                if (it->getMessageId() == m.id)
                    item = it;

            if (item == nullptr)
            {
                item = items.add (new MessageItem (session, m.id));
                item->onLayoutChanged = [this] { if (onLayoutChanged) onLayoutChanged(); };
                addAndMakeVisible (item);
                changed = true;
            }

            changed = item->setMessage (m) || changed;
        }

        return changed;
    }

    int relayout (int width)
    {
        int y = 10;
        for (auto* item : items)
        {
            const int h = item->layoutForWidth (width - 16);
            item->setBounds (8, y, width - 16, h);
            y += h;
        }

        setSize (width, y + 6);
        repaint();
        return y;
    }

    bool hasStatus() const
    {
        for (auto* item : items)
            if (item->isStatus())
                return true;
        return false;
    }

    void repaintStatus()
    {
        for (auto* item : items)
            if (item->isStatus())
                item->repaint();
    }

    std::function<void()> onLayoutChanged;

private:
    ChatSession& session;
    juce::OwnedArray<MessageItem> items;
};

//==============================================================================
ChatPanel::ChatPanel (ChatSession& s) : session (s)
{
    list = std::make_unique<MessageList> (session);
    list->onLayoutChanged = [this] { list->relayout (viewport.getMaximumVisibleWidth()); };
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    // V1 input styling and placeholder.
    input.setMultiLine (false);
    input.setTextToShowWhenEmpty ("Type your request here... e.g. \"make it warmer\" (press Enter)", juce::Colour (0xff888888));
    input.setFont (font (14.0f));
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff252525));
    input.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    input.setIndents (10, 0);
    input.setJustification (juce::Justification::centredLeft);
    input.addListener (this);
    input.addKeyListener (this);
    addAndMakeVisible (input);

    setStyle (sendButton, "primary");
    sendButton.onClick = [this] { sendText (input.getText()); };
    addAndMakeVisible (sendButton);

    session.addChangeListener (this);
    refreshMessages();
    refreshChips();
    startTimerHz (10);
}

ChatPanel::~ChatPanel()
{
    session.removeChangeListener (this);
    input.removeKeyListener (this);
}

void ChatPanel::sendText (const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty())
        return;

    sent.add (t);
    sentIndex = -1;
    input.clear();
    session.sendUserMessage (t);

    // Always follow the conversation after the user sends something.
    list->relayout (viewport.getMaximumVisibleWidth());
    viewport.setViewPosition (0, juce::jmax (0, list->getHeight() - viewport.getHeight()));
}

void ChatPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshMessages();
    refreshChips();
}

void ChatPanel::refreshMessages()
{
    if (shownVersion == session.getVersion())
        return;
    shownVersion = session.getVersion();

    const bool atBottom = viewport.getViewPositionY() >= list->getHeight() - viewport.getHeight() - 40;
    if (list->sync (session.getMessages()))
    {
        list->relayout (viewport.getMaximumVisibleWidth());
        if (atBottom)
            viewport.setViewPosition (0, juce::jmax (0, list->getHeight() - viewport.getHeight()));
    }
    repaint (getLocalBounds().removeFromTop (40));
}

void ChatPanel::refreshChips()
{
    const auto suggestions = session.getSuggestions();

    bool same = suggestions.size() == chips.size();
    for (int i = 0; same && i < chips.size(); ++i)
        same = chips[i]->getButtonText() == suggestions[i];
    if (same)
        return;

    chips.clear();
    for (auto& s : suggestions)
    {
        auto* b = chips.add (new juce::TextButton (s));
        setStyle (*b, "chip");
        b->onClick = [this, s] { sendText (s); };
        addAndMakeVisible (b);
    }
    layoutChips();
}

void ChatPanel::layoutChips()
{
    int x = chipArea.getX();
    for (auto* b : chips)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (font (12.0f), b->getButtonText()) + 24;
        const bool fits = x + w <= chipArea.getRight();
        b->setVisible (fits);
        if (fits)
            b->setBounds (x, chipArea.getY(), w, chipArea.getHeight());
        x += w + 6;
    }
}

void ChatPanel::textEditorReturnKeyPressed (juce::TextEditor&)
{
    sendText (input.getText());
}

bool ChatPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (sent.isEmpty())
        return false;

    if (key == juce::KeyPress::upKey)
    {
        sentIndex = sentIndex < 0 ? sent.size() - 1 : juce::jmax (0, sentIndex - 1);
        input.setText (sent[sentIndex], false);
        input.moveCaretToEnd();
        return true;
    }

    if (key == juce::KeyPress::downKey && sentIndex >= 0)
    {
        sentIndex = sentIndex + 1 < sent.size() ? sentIndex + 1 : -1;
        input.setText (sentIndex >= 0 ? sent[sentIndex] : juce::String(), false);
        input.moveCaretToEnd();
        return true;
    }

    return false;
}

void ChatPanel::timerCallback()
{
    if (list->hasStatus())
        list->repaintStatus();
}

void ChatPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    drawPanel (g, bounds.toFloat());

    // V1 header: gradient, green dot, "AI CHAT", "ONLINE"
    auto header = bounds.removeFromTop (40);
    juce::ColourGradient grad (juce::Colour (0xff333333), 0, 0, juce::Colour (0xff222222), 0, (float) header.getHeight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (header.toFloat().reduced (1.0f), 6.0f);

    const bool busy = session.isBusy();
    g.setColour (busy ? Palette::amber : Palette::accent);
    g.fillEllipse (12.0f, (float) header.getY() + 14.0f, 12.0f, 12.0f);

    g.setColour (juce::Colours::white);
    g.setFont (font (15.0f, true));
    g.drawText ("AI CHAT", header.withTrimmedLeft (32), juce::Justification::centredLeft);

    g.setColour (busy ? Palette::amber : Palette::accent);
    g.setFont (font (12.0f, true));
    g.drawText (busy ? "WORKING" : "ONLINE", header.withTrimmedRight (12), juce::Justification::centredRight);

    g.setColour (Palette::textFaint);
    g.setFont (font (11.0f));
    g.drawText ("local assistant - changes need your approval", header.withTrimmedLeft (110).withTrimmedRight (90),
                juce::Justification::centredLeft);
}

void ChatPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (42);
    r.reduce (8, 8);

    auto inputRow = r.removeFromBottom (34);
    sendButton.setBounds (inputRow.removeFromRight (80));
    inputRow.removeFromRight (8);
    input.setBounds (inputRow);

    r.removeFromBottom (8);
    chipArea = r.removeFromBottom (24);
    r.removeFromBottom (6);
    layoutChips();

    viewport.setBounds (r);
    list->relayout (viewport.getMaximumVisibleWidth());
    viewport.setViewPosition (0, juce::jmax (0, list->getHeight() - viewport.getHeight()));
}

} // namespace prizm::ui
