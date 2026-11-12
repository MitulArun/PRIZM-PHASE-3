/*
  ==============================================================================

    PRIZM V2 - undo / redo / restore.

  ==============================================================================
*/

#include "HistoryManager.h"

namespace prizm
{

namespace
{
    constexpr size_t maxEntries = 100;
    constexpr juce::uint32 gestureMergeMs = 1500;
}

HistoryManager::HistoryManager (ParamStateHost& h, const juce::Array<juce::AudioProcessorParameter*>& params)
    : host (h), parameters (params)
{
    for (auto* p : parameters)
        p->addListener (this);
}

HistoryManager::~HistoryManager()
{
    for (auto* p : parameters)
        p->removeListener (this);
}

void HistoryManager::trim()
{
    if (undoStack.size() > maxEntries)
        undoStack.erase (undoStack.begin(), undoStack.begin() + (std::ptrdiff_t) (undoStack.size() - maxEntries));
}

int HistoryManager::pushUndoPoint (const juce::String& label)
{
    JUCE_ASSERT_MESSAGE_THREAD
    Entry e { nextId++, host.getParamState(), label, juce::Time::getCurrentTime() };
    undoStack.push_back (e);
    redoStack.clear();
    trim();
    lastGestureParam = -1;
    sendChangeMessage();
    return e.id;
}

int HistoryManager::applyWithUndo (const ParamState& newState, const juce::String& label)
{
    const int id = pushUndoPoint (label);
    apply (newState);
    return id;
}

void HistoryManager::apply (const ParamState& state)
{
    const juce::ScopedValueSetter<bool> svs (applying, true);
    host.setParamState (state);
}

bool HistoryManager::undo()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (undoStack.empty())
        return false;

    Entry target = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ target.id, host.getParamState(), target.label, juce::Time::getCurrentTime() });
    apply (target.state);
    lastGestureParam = -1;
    sendChangeMessage();
    return true;
}

bool HistoryManager::redo()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (redoStack.empty())
        return false;

    Entry target = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ target.id, host.getParamState(), target.label, juce::Time::getCurrentTime() });
    apply (target.state);
    lastGestureParam = -1;
    sendChangeMessage();
    return true;
}

bool HistoryManager::restoreBefore (int id)
{
    for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it)
    {
        if (it->id == id)
        {
            const auto state = it->state;
            applyWithUndo (state, "Restore state before \"" + it->label + "\"");
            return true;
        }
    }
    return false;
}

void HistoryManager::captureSessionStart()
{
    sessionStart = host.getParamState();
    hasSessionStart = true;
}

bool HistoryManager::restoreSessionStart()
{
    if (! hasSessionStart)
        return false;

    applyWithUndo (sessionStart, "Restore original settings");
    return true;
}

void HistoryManager::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    // Only user gestures from PRIZM's own UI or the host's generic editor on the
    // message thread are recorded; programmatic changes (undo, proposals) are not.
    if (! gestureIsStarting || applying || ! juce::MessageManager::existsAndIsCurrentThread())
        return;

    const auto now = juce::Time::getMillisecondCounter();
    if (parameterIndex == lastGestureParam && now - lastGestureMs < gestureMergeMs)
    {
        lastGestureMs = now;
        return;
    }

    const juce::String name = parameterIndex >= 0 && parameterIndex < parameters.size()
                                  ? parameters[parameterIndex]->getName (64) : juce::String ("parameter");
    pushUndoPoint ("Adjust " + name);
    lastGestureParam = parameterIndex;
    lastGestureMs = now;
}

} // namespace prizm
