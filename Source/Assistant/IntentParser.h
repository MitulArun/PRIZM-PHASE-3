/*
  ==============================================================================

    PRIZM V2 - natural-language intent parser (local, deterministic).

    Turns messages like "make it a little warmer but less harsh",
    "boost 3k by 2 dB", "master it for Spotify" or "turn off the limiter"
    into structured intents. Handles:
      - clause splitting ("... but ...", "... and ...", commas),
      - intensity words ("a touch", "slightly", "much", "really"),
      - direction ("less bright", "too much bass", "not enough low end",
        "darker" vs "too dark"),
      - numbers with units (Hz/kHz/k, dB, LUFS, ms, %, ratios),
      - streaming platform loudness targets.

    Word matching is boundary-aware (V1 matched "hi" inside "this").

  ==============================================================================
*/

#pragma once

#include "../Common/ParamTable.h"

#include <optional>
#include <string>
#include <vector>

namespace prizm::assistant
{

enum class IntentType
{
    // Tonal
    Warmth, Brightness, Air, Bass, Sub, Mud, Boxy, Harsh, Presence, Sibilance,
    // Dynamics
    Punch, Glue, Tight, MoreDynamic, CompSetting,
    // Loudness
    Louder, LoudnessTarget, Ceiling,
    // Colour / effects
    Saturate, Vintage, Telephone,
    // Stereo
    Wider, MonoBass,
    // Explicit EQ
    EqBand, LowCut, HighCut,
    // Modules
    ModuleOn, ModuleOff,
    // Assistant workflows
    Master, MatchReference, Analyze, LoadReference,
    // Session
    Undo, Redo, Reset, Compare, MonoCheck, Apply, Dismiss,
    // Conversation
    Help, Greeting, Thanks, Explain, Status
};

enum class CompField { None, Threshold, Ratio, Attack, Release, Makeup, Knee, Mix };

struct Intent
{
    explicit Intent (IntentType t) : type (t) {}

    IntentType type;
    float direction = 1.0f;      // +1 more of the quality, -1 less
    float intensity = 1.0f;      // 0.5 subtle .. 2 extreme
    std::optional<float> freqHz, gainDb, lufs, ms, ratio, percent, plainNumber;
    Module module = Module::Global;
    CompField compField = CompField::None;
    int saturationType = -1;     // 0 tape, 1 tube, 2 soft clip
    std::string platform;        // "Spotify", "Apple Music", ...
    float ceilingDb = -1.0f;     // suggested by platform
    std::string phrase;          // the words that triggered it (for explanations)
};

struct ParsedMessage
{
    std::vector<Intent> intents;
    bool isQuestion = false;

    bool has (IntentType t) const noexcept
    {
        for (auto& i : intents)
            if (i.type == t)
                return true;
        return false;
    }

    const Intent* find (IntentType t) const noexcept
    {
        for (auto& i : intents)
            if (i.type == t)
                return &i;
        return nullptr;
    }
};

ParsedMessage parseMessage (const std::string& text);

/** Lower-cases, normalises unicode minus/dashes and collapses whitespace. */
std::string normaliseText (const std::string& text);

} // namespace prizm::assistant
