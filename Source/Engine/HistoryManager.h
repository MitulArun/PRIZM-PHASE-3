/*
  ==============================================================================

    PRIZM V2 - undo / redo / restore for every parameter change.

    A snapshot of the complete parameter state is taken *before* each change:
      - before the user applies an assistant proposal,
      - before each manual control gesture (consecutive tweaks of the same
        control within 1.5 s are merged into one step),
      - before reset / restore operations.
    Snapshots are restored through the normal parameter path, so the DSP
    smoothing makes undo/redo glitch-free and the host sees the changes.

    Message thread only.

  ==============================================================================
*/

#pragma once

#include "../Common/ParamTable.h"
#include <JuceHeader.h>

namespace prizm
{

class ParamStateHost
{
public:
    virtual ~ParamStateHost() = default;
    virtual ParamState getParamState() const = 0;
    virtual void setParamState (const ParamState& state) = 0;
};

class HistoryManager : public juce::ChangeBroadcaster,
                       private juce::AudioProcessorParameter::Listener
{
public:
    struct Entry
    {
        int id = 0;
        ParamState state {};
        juce::String label;     // describes the change that was made *after* this snapshot
        juce::Time time;
    };

    HistoryManager (ParamStateHost& host, const juce::Array<juce::AudioProcessorParameter*>& params);
    ~HistoryManager() override;

    /** Snapshot the current state as an undo point, then apply 'newState'. */
    int applyWithUndo (const ParamState& newState, const juce::String& label);

    /** Snapshot the current state as an undo point (the caller changes parameters afterwards). */
    int pushUndoPoint (const juce::String& label);

    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::String getUndoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    juce::String getRedoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().label; }

    /** Restores the state saved before entry 'id' (itself undoable). */
    bool restoreBefore (int id);

    /** The state when the session/plugin was opened. */
    void captureSessionStart();
    bool restoreSessionStart();

    const std::vector<Entry>& getUndoEntries() const noexcept { return undoStack; }

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void apply (const ParamState& state);
    void trim();

    ParamStateHost& host;
    juce::Array<juce::AudioProcessorParameter*> parameters;
    std::vector<Entry> undoStack, redoStack;
    ParamState sessionStart {};
    bool hasSessionStart = false;
    bool applying = false;
    int nextId = 1;

    int lastGestureParam = -1;
    juce::uint32 lastGestureMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryManager)
};

} // namespace prizm
