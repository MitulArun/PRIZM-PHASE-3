/*
  ==============================================================================

    PRIZM V2 - redesigned AI chat panel.

    Replaces V1's ChatInterface (one editable TextEditor + canned replies)
    with message bubbles, proposal cards wired to real parameters (per-change
    checkboxes, Apply / Dismiss / Undo), live job progress, suggestion chips,
    smart auto-scroll and input history. Keeps V1's "AI CHAT - ONLINE" header,
    input placeholder style and blue Send button.

  ==============================================================================
*/

#pragma once

#include "../Engine/ChatSession.h"
#include "Controls.h"

namespace prizm::ui
{

class ChatPanel : public juce::Component,
                  private juce::ChangeListener,
                  private juce::TextEditor::Listener,
                  private juce::KeyListener,
                  private juce::Timer
{
public:
    explicit ChatPanel (ChatSession& session);
    ~ChatPanel() override;

    void sendText (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;

    class MessageItem;
    class MessageList;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;

    void refreshMessages();
    void refreshChips();
    void layoutChips();

    ChatSession& session;
    juce::Viewport viewport;
    std::unique_ptr<MessageList> list;
    juce::TextEditor input;
    juce::TextButton sendButton { "Send" };
    juce::OwnedArray<juce::TextButton> chips;
    juce::Rectangle<int> chipArea;

    juce::StringArray sent;
    int sentIndex = -1;
    int shownVersion = -1;
};

} // namespace prizm::ui
