/*
  ==============================================================================

    PRIZM V2 - single source of truth for every plugin parameter.

    The JUCE layer builds its AudioProcessorValueTreeState layout from this
    table, the DSP chain reads a ParamState snapshot of it, and the assistant
    proposes changes in terms of it. Plain C++ so it can be unit tested.

  ==============================================================================
*/

#pragma once

#include <array>
#include <string>
#include <string_view>

namespace prizm
{

enum ParamIndex : int
{
    pInGain, pOutGain,

    pEqOn,
    pEqHpfOn, pEqHpfFreq,
    pEqLsFreq, pEqLsGain, pEqLsQ,
    pEqP1Freq, pEqP1Gain, pEqP1Q,
    pEqP2Freq, pEqP2Gain, pEqP2Q,
    pEqP3Freq, pEqP3Gain, pEqP3Q,
    pEqHsFreq, pEqHsGain, pEqHsQ,
    pEqLpfOn, pEqLpfFreq,

    pCompOn, pCompThresh, pCompRatio, pCompAttack, pCompRelease,
    pCompKnee, pCompMakeup, pCompMix, pCompScHpf,

    pSatOn, pSatType, pSatDrive, pSatMix,

    pImgOn, pImgWidth, pImgMonoOn, pImgMonoFreq,

    pLimOn, pLimGain, pLimCeiling, pLimRelease, pLimTruePeak,

    NumParams
};

enum class ParamKind { Float, Bool, Choice };

enum class Module { Global, EQ, Compressor, Saturation, Imager, Limiter };

struct ParamInfo
{
    const char* id;
    const char* name;
    Module module;
    ParamKind kind;
    float minValue, maxValue, defaultValue;
    float skewCentre;       // 0 = linear mapping, otherwise value shown at the slider centre
    const char* unit;       // "dB", "Hz", "ms", "%", ":1", "" ...
    const char* choices;    // '|' separated, only for ParamKind::Choice
};

using ParamState = std::array<float, NumParams>;

const ParamInfo& paramInfo (int index) noexcept;
ParamState defaultParamState() noexcept;
int findParam (std::string_view id) noexcept;     // -1 if not found
float clampParam (int index, float value) noexcept;
std::string formatParamValue (int index, float value);
const char* moduleName (Module m) noexcept;
const char* moduleEnableParamId (Module m) noexcept;
int moduleEnableParam (Module m) noexcept;         // -1 for Global

inline bool isOn (const ParamState& s, int index) noexcept { return s[(size_t) index] >= 0.5f; }

} // namespace prizm
