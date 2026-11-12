/*
  ==============================================================================

    PRIZM V2 - chat assistant: messages in, replies / proposals / actions out.

    Replaces V1's canned processStaticChat(). Every request that would change
    the sound becomes a Proposal (shown as a card with Apply / Dismiss); the
    assistant never changes parameters by itself. Workflow requests (master,
    match reference, analyse, loudness targets) are returned as actions the
    plugin runs on its background thread.

  ==============================================================================
*/

#pragma once

#include "IntentParser.h"
#include "MasteringAssistant.h"

#include <optional>

namespace prizm::assistant
{

/** Real-time measurements from the plugin's meters (message-thread snapshot). */
struct LiveContext
{
    bool audioPlaying = false;
    float inputShortTermLufs = -144.0f;
    float inputRmsDb = -144.0f;              // ~300 ms RMS of the input (dBFS)
    float outputShortTermLufs = -144.0f;
    float outputIntegratedLufs = -144.0f;
    float outputTruePeakDb = -144.0f;        // max since meter reset
    float compGrDb = 0.0f, limGrDb = 0.0f;
    float correlation = 1.0f;
    double sampleRate = 48000.0;
};

struct AssistantContext
{
    ParamState params {};
    LiveContext live;
    const dsp::AnalysisResult* recentInput = nullptr;   // analysis of audio PRIZM heard recently
    const dsp::AnalysisResult* reference = nullptr;
    std::string referenceName;
    bool hasPendingProposal = false;
    bool canUndo = false, canRedo = false;
    bool comparing = false, monoCheck = false;
    float targetLufs = -14.0f, ceilingDb = -1.0f;
    std::string lastAppliedTitle;
};

enum class ActionType
{
    None,
    ApplyPending, DismissPending,
    Undo, Redo,
    ToggleCompare, ToggleMonoCheck,
    LoadReference,
    RunMaster, RunAnalysis
};

struct Reply
{
    std::string text;
    std::optional<Proposal> proposal;
    ActionType action = ActionType::None;
    MasterRequest master;            // for RunMaster
    bool analyseOutput = false;      // for RunAnalysis
};

Reply respond (const std::string& message, const AssistantContext& context);

std::string helpText();
std::string welcomeText();

} // namespace prizm::assistant
