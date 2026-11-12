/*
  ==============================================================================

    PRIZM V2 - chat session: transcript, proposal cards and workflow actions.

    Owned by the processor so the conversation survives closing and reopening
    the plugin window. Message thread only.

  ==============================================================================
*/

#pragma once

#include "../Assistant/ChatAssistant.h"
#include <JuceHeader.h>

class Prizmv1AudioProcessor;

namespace prizm
{

struct ChatMessage
{
    enum class Role { User, Assistant, Status };
    enum class CardState { None, Pending, Applied, Dismissed, Undone };

    int id = 0;
    Role role = Role::Assistant;
    juce::String text;
    std::optional<assistant::Proposal> proposal;
    CardState cardState = CardState::None;
    int historyId = -1;          // undo point created when the card was applied
    int jobId = 0;               // for Status messages tracking a worker job
    float progress = -1.0f;
    juce::Time time;
};

class ChatSession : public juce::ChangeBroadcaster,
                    private juce::Timer
{
public:
    explicit ChatSession (Prizmv1AudioProcessor& processor);
    ~ChatSession() override;

    void sendUserMessage (const juce::String& text);

    void applyProposal (int messageId);
    void dismissProposal (int messageId);
    void undoProposal (int messageId);
    void setChangeSelected (int messageId, int changeIndex, bool selected);

    /** Called by the editor once the user picked a reference file. */
    void loadReference (const juce::File& file);

    const std::vector<ChatMessage>& getMessages() const noexcept { return messages; }
    int getVersion() const noexcept { return version; }
    bool isBusy() const noexcept { return activeJobId != 0; }
    juce::StringArray getSuggestions() const;

    /** Most recent analysis of the user's (unprocessed) track, if any. */
    const dsp::AnalysisResult* getRecentInputAnalysis() const noexcept { return recentInput ? &*recentInput : nullptr; }
    const dsp::AnalysisResult* getRecentOutputAnalysis() const noexcept { return recentOutput ? &*recentOutput : nullptr; }

    /** Asks the worker to analyse the track; result arrives as a chat message and updates the reference panel. */
    void requestTrackAnalysis (bool processedOutput);

    /** Editor hook: open a file chooser for a reference track. */
    std::function<void()> onRequestReferenceFile;

private:
    void timerCallback() override;
    void handleReply (const assistant::Reply& reply);
    int addMessage (ChatMessage::Role role, const juce::String& text, std::optional<assistant::Proposal> proposal = {});
    ChatMessage* findMessage (int id);
    void startJob (int jobId, const juce::String& text);
    void finishJob (const juce::String& text, std::optional<assistant::Proposal> proposal = {});
    assistant::AssistantContext buildContext() const;
    void changed();

    Prizmv1AudioProcessor& processor;
    std::vector<ChatMessage> messages;
    int nextMessageId = 1, version = 0;
    int activeJobId = 0, activeJobMessageId = 0;
    bool lastAnalysisWasOutput = false;
    juce::String lastAppliedTitle;

    std::optional<dsp::AnalysisResult> recentInput, recentOutput;
    mutable dsp::AnalysisResult contextReference;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChatSession)
};

} // namespace prizm
