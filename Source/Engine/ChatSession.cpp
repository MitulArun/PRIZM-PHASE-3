/*
  ==============================================================================

    PRIZM V2 - chat session.

  ==============================================================================
*/

#include "ChatSession.h"
#include "../PluginProcessor.h"

namespace prizm
{

ChatSession::ChatSession (Prizmv1AudioProcessor& p) : processor (p)
{
    addMessage (ChatMessage::Role::Assistant, assistant::welcomeText());
    startTimerHz (15);
}

ChatSession::~ChatSession()
{
    stopTimer();
}

void ChatSession::changed()
{
    ++version;
    sendChangeMessage();
}

int ChatSession::addMessage (ChatMessage::Role role, const juce::String& text, std::optional<assistant::Proposal> proposal)
{
    ChatMessage m;
    m.id = nextMessageId++;
    m.role = role;
    m.text = text;
    m.time = juce::Time::getCurrentTime();

    if (proposal && ! proposal->empty())
    {
        proposal->id = m.id;
        m.proposal = std::move (proposal);
        m.cardState = ChatMessage::CardState::Pending;
    }

    messages.push_back (std::move (m));

    // Keep the transcript bounded.
    if (messages.size() > 200)
        messages.erase (messages.begin(), messages.begin() + 50);

    changed();
    return messages.back().id;
}

ChatMessage* ChatSession::findMessage (int id)
{
    for (auto& m : messages)
        if (m.id == id)
            return &m;
    return nullptr;
}

assistant::AssistantContext ChatSession::buildContext() const
{
    assistant::AssistantContext ctx;
    ctx.params = processor.getParamState();

    const auto m = processor.getMeters();
    ctx.live.audioPlaying = m.audioActive;
    ctx.live.inputShortTermLufs = m.inShortTerm;
    ctx.live.inputRmsDb = dsp::gainToDb (std::max (m.inRms[0], m.inRms[1]), -144.0f);
    ctx.live.outputShortTermLufs = m.outShortTerm;
    ctx.live.outputIntegratedLufs = m.outIntegrated;
    ctx.live.outputTruePeakDb = m.outTruePeakMaxDb;
    ctx.live.compGrDb = m.compGrDb;
    ctx.live.limGrDb = m.limGrDb;
    ctx.live.correlation = m.correlation;
    ctx.live.sampleRate = processor.getCurrentSampleRate();

    ctx.recentInput = recentInput ? &*recentInput : nullptr;

    // The reference lives in the processor (persisted); keep a copy alive for the call.
    const auto ref = processor.getReference();
    contextReference = ref.analysis;
    ctx.reference = ref.valid ? &contextReference : nullptr;
    ctx.referenceName = ref.name.toStdString();

    for (auto& msg : messages)
        if (msg.cardState == ChatMessage::CardState::Pending)
            ctx.hasPendingProposal = true;

    ctx.canUndo = processor.getHistory().canUndo();
    ctx.canRedo = processor.getHistory().canRedo();
    ctx.comparing = processor.isComparingOriginal();
    ctx.monoCheck = processor.isMonoCheck();
    ctx.targetLufs = processor.getTargetLufs();
    ctx.ceilingDb = processor.getCeilingDb();
    ctx.lastAppliedTitle = lastAppliedTitle.toStdString();
    return ctx;
}

void ChatSession::sendUserMessage (const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return;

    addMessage (ChatMessage::Role::User, trimmed);
    handleReply (assistant::respond (trimmed.toStdString(), buildContext()));
}

void ChatSession::handleReply (const assistant::Reply& reply)
{
    using A = assistant::ActionType;
    auto& history = processor.getHistory();

    switch (reply.action)
    {
        case A::ApplyPending:
        {
            for (auto it = messages.rbegin(); it != messages.rend(); ++it)
            {
                if (it->cardState == ChatMessage::CardState::Pending)
                {
                    applyProposal (it->id);
                    return;
                }
            }
            break;
        }

        case A::DismissPending:
            for (auto& m : messages)
                if (m.cardState == ChatMessage::CardState::Pending)
                    m.cardState = ChatMessage::CardState::Dismissed;
            break;

        case A::Undo:
            if (history.canUndo())
            {
                const auto label = history.getUndoLabel();
                history.undo();
                for (auto& m : messages)
                    if (m.cardState == ChatMessage::CardState::Applied && m.proposal && label == juce::String (m.proposal->title))
                        m.cardState = ChatMessage::CardState::Undone;
                addMessage (ChatMessage::Role::Assistant, "Undone: **" + label + "**. Say \"redo\" to bring it back.");
                return;
            }
            break;

        case A::Redo:
            if (history.canRedo())
            {
                const auto label = history.getRedoLabel();
                history.redo();
                addMessage (ChatMessage::Role::Assistant, "Redone: **" + label + "**.");
                return;
            }
            break;

        case A::ToggleCompare:
        {
            const bool now = ! processor.isComparingOriginal();
            processor.setCompareOriginal (now);
            addMessage (ChatMessage::Role::Assistant,
                        now ? juce::String ("Now playing the **original** (unprocessed")
                                  + (processor.isLevelMatch() ? ", level-matched so you judge tone, not loudness" : "")
                                  + "). Say \"compare\" again or click **Processed** to switch back."
                            : juce::String ("Back to the **processed** sound."));
            return;
        }

        case A::ToggleMonoCheck:
        {
            const bool now = ! processor.isMonoCheck();
            processor.setMonoCheck (now);
            addMessage (ChatMessage::Role::Assistant,
                        now ? "Mono check **on** - listen for anything that disappears or gets thin; that's a phase problem."
                            : "Mono check **off**.");
            return;
        }

        case A::LoadReference:
            if (onRequestReferenceFile)
                onRequestReferenceFile();
            break;

        case A::RunMaster:
        {
            if (activeJobId != 0)
            {
                addMessage (ChatMessage::Role::Assistant, "I'm still working on the previous request - one moment.");
                return;
            }

            if (! reply.master.loudnessOnly && ! reply.master.matchReference)
            {
                processor.setTargetLufs (reply.master.targetLufs);
                processor.setCeilingDb (reply.master.ceilingDb);
            }

            std::optional<dsp::AnalysisResult> ref;
            const auto info = processor.getReference();
            if (info.valid)
                ref = info.analysis;

            addMessage (ChatMessage::Role::Assistant, reply.text);
            startJob (processor.getWorker().requestMaster (reply.master, processor.getParamState(), ref), "Waiting for audio...");
            return;
        }

        case A::RunAnalysis:
            addMessage (ChatMessage::Role::Assistant, reply.text);
            requestTrackAnalysis (false);
            return;

        case A::None:
            break;
    }

    if (reply.text.empty() && ! reply.proposal)
        return;

    addMessage (ChatMessage::Role::Assistant, reply.text, reply.proposal);
}

//==============================================================================
void ChatSession::applyProposal (int messageId)
{
    auto* m = findMessage (messageId);
    if (m == nullptr || ! m->proposal || m->cardState != ChatMessage::CardState::Pending)
        return;

    if (m->proposal->numSelected() == 0)
    {
        addMessage (ChatMessage::Role::Assistant, "Nothing is selected in that suggestion - tick at least one change to apply.");
        return;
    }

    auto& history = processor.getHistory();
    const auto current = processor.getParamState();
    const auto target = m->proposal->applyTo (current);
    m->historyId = history.applyWithUndo (target, m->proposal->title);
    m->cardState = ChatMessage::CardState::Applied;
    lastAppliedTitle = m->proposal->title;

    const int applied = m->proposal->numSelected();
    const int total = (int) m->proposal->changes.size();
    juce::String text = "Applied **" + juce::String (m->proposal->title) + "** (" + juce::String (applied) + " change" + (applied == 1 ? "" : "s");
    if (applied < total)
        text << " of " << total;
    text << "). Use **Original / Processed** to compare, or say \"undo\" to revert.";

    if (m->proposal->prediction.valid && applied < total)
        text << " The loudness prediction assumed every change, so the result may differ slightly.";

    if (processor.isComparingOriginal())
    {
        processor.setCompareOriginal (false);
        text << " (Switched back to the processed sound.)";
    }

    addMessage (ChatMessage::Role::Assistant, text);
}

void ChatSession::dismissProposal (int messageId)
{
    if (auto* m = findMessage (messageId))
    {
        if (m->cardState == ChatMessage::CardState::Pending)
        {
            m->cardState = ChatMessage::CardState::Dismissed;
            changed();
        }
    }
}

void ChatSession::undoProposal (int messageId)
{
    auto* m = findMessage (messageId);
    if (m == nullptr || m->cardState != ChatMessage::CardState::Applied)
        return;

    auto& history = processor.getHistory();
    const auto& entries = history.getUndoEntries();

    if (! entries.empty() && entries.back().id == m->historyId)
        history.undo();                       // it's the latest change: plain undo
    else if (! history.restoreBefore (m->historyId))
        return;                               // too old - no longer in the history

    m->cardState = ChatMessage::CardState::Undone;
    addMessage (ChatMessage::Role::Assistant, "Reverted **" + juce::String (m->proposal->title) + "** - your previous settings are back.");
}

void ChatSession::setChangeSelected (int messageId, int changeIndex, bool selected)
{
    if (auto* m = findMessage (messageId))
    {
        if (m->proposal && changeIndex >= 0 && changeIndex < (int) m->proposal->changes.size()
            && m->cardState == ChatMessage::CardState::Pending)
        {
            m->proposal->changes[(size_t) changeIndex].selected = selected;
            changed();
        }
    }
}

//==============================================================================
void ChatSession::loadReference (const juce::File& file)
{
    addMessage (ChatMessage::Role::Assistant, "Analysing **" + file.getFileName() + "** in the background (it is never mixed into your audio)...");
    startJob (processor.getWorker().requestReference (file), "Reading " + file.getFileName() + "...");
}

void ChatSession::requestTrackAnalysis (bool processedOutput)
{
    if (activeJobId != 0)
        return;

    lastAnalysisWasOutput = processedOutput;
    startJob (processor.getWorker().requestAnalysis (processedOutput), "Waiting for audio...");
}

void ChatSession::startJob (int jobId, const juce::String& text)
{
    activeJobId = jobId;
    activeJobMessageId = addMessage (ChatMessage::Role::Status, text);
    if (auto* m = findMessage (activeJobMessageId))
        m->jobId = jobId;
}

void ChatSession::finishJob (const juce::String& text, std::optional<assistant::Proposal> proposal)
{
    // Replace the progress line with the result.
    messages.erase (std::remove_if (messages.begin(), messages.end(),
                                    [this] (const ChatMessage& m) { return m.id == activeJobMessageId; }),
                    messages.end());
    activeJobId = 0;
    activeJobMessageId = 0;
    addMessage (ChatMessage::Role::Assistant, text, std::move (proposal));
}

void ChatSession::timerCallback()
{
    if (processor.sessionStartPending.exchange (false))
        processor.getHistory().captureSessionStart();

    if (activeJobId != 0)
    {
        const auto status = processor.getWorker().getStatus();
        if (auto* m = findMessage (activeJobMessageId))
        {
            if (status.busy && status.requestId == activeJobId && (m->text != status.message || m->progress != status.progress))
            {
                m->text = status.message;
                m->progress = status.progress;
                changed();
            }
        }
    }

    WorkerResult r;
    while (processor.getWorker().popResult (r))
    {
        if (r.requestId != activeJobId)
            continue;

        using K = WorkerResult::Kind;

        if (r.kind == K::Failed)
        {
            finishJob (r.error);
            continue;
        }

        if (r.kind == K::Reference)
        {
            Prizmv1AudioProcessor::ReferenceInfo info;
            info.name = r.fileName;
            info.path = r.filePath;
            info.analysis = r.analysis;
            info.valid = true;
            processor.setReference (info);

            juce::String text;
            text << "Reference loaded: **" << r.fileName << "**\n"
                 << juce::String::formatted ("- %.1f LUFS integrated, true peak %.1f dBTP, PLR %.1f dB, loudness range %.1f LU\n",
                                             r.analysis.integratedLufs, r.analysis.truePeakDb, r.analysis.plrDb, r.analysis.loudnessRange)
                 << juce::String::formatted ("- Tonal tilt %.1f dB/oct, stereo correlation %.2f\n", r.analysis.tiltDbPerOctave, r.analysis.correlation)
                 << "Its curve is now shown in the spectrum. Say **compare with the reference** or **match the reference**.";
            finishJob (text);
            continue;
        }

        if (r.kind == K::Analysis)
        {
            (r.analysedOutput ? recentOutput : recentInput) = r.analysis;
            const auto ref = processor.getReference();
            finishJob (assistant::describeAnalysis (r.analysis, ref.valid ? &ref.analysis : nullptr,
                                                   processor.getTargetLufs(), r.analysedOutput));
            continue;
        }

        if (r.kind == K::Master)
        {
            recentInput = r.analysis;
            auto& p = r.proposal;

            if (p.empty())
            {
                finishJob ("Your track already matches the target closely - I don't have anything worth changing.");
                continue;
            }

            juce::String text;
            text << "**" << p.title << "** - " << p.summary;
            if (p.prediction.valid)
                text << juce::String::formatted ("\nPredicted for the %.0f s I heard: **%.1f LUFS**, true peak %.1f dBTP, limiter ~%.1f dB on loud parts. "
                                                 "Whole-song loudness can differ slightly - check the Integrated meter after a full play-through.",
                                                 r.secondsUsed, p.prediction.integratedLufs, p.prediction.truePeakDb, p.prediction.avgLimiterGrDb);
            text << "\nReview the changes below - untick anything you don't want - then **Apply**.";
            finishJob (text, p);
        }
    }
}

juce::StringArray ChatSession::getSuggestions() const
{
    juce::StringArray s;
    bool pending = false;
    for (auto& m : messages)
        pending = pending || m.cardState == ChatMessage::CardState::Pending;

    const bool hasRef = processor.getReference().valid;

    if (pending)
        s.add ("Apply");
    s.add ("Master for streaming");
    if (hasRef)
        s.add ("Match the reference");
    else
        s.add ("Load reference");
    s.add ("Analyze my mix");
    s.add ("Make it warmer");
    s.add ("Less harsh");
    if (processor.getHistory().canUndo())
        s.add ("Undo");
    s.add ("Help");
    return s;
}

} // namespace prizm
