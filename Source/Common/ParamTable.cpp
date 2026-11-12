/*
  ==============================================================================

    PRIZM V2 - parameter table.

  ==============================================================================
*/

#include "ParamTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prizm
{

namespace
{
    using K = ParamKind;
    using M = Module;

    // Order must match ParamIndex.
    const ParamInfo table[NumParams] =
    {
        { "in_gain",      "Input Gain",        M::Global,     K::Float,  -24.0f,    24.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "out_gain",     "Output Gain",       M::Global,     K::Float,  -24.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },

        { "eq_on",        "EQ On",             M::EQ,         K::Bool,     0.0f,     1.0f,     1.0f,   0.0f,     "",   nullptr },
        { "eq_hpf_on",    "Low Cut On",        M::EQ,         K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "eq_hpf_freq",  "Low Cut Freq",      M::EQ,         K::Float,   10.0f,   400.0f,    30.0f,  60.0f,     "Hz", nullptr },
        { "eq_ls_freq",   "Low Shelf Freq",    M::EQ,         K::Float,   20.0f,  1000.0f,   100.0f, 150.0f,     "Hz", nullptr },
        { "eq_ls_gain",   "Low Shelf Gain",    M::EQ,         K::Float,  -12.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "eq_ls_q",      "Low Shelf Q",       M::EQ,         K::Float,    0.3f,     2.0f,   0.707f,   0.0f,     "",   nullptr },
        { "eq_p1_freq",   "Bell 1 Freq",       M::EQ,         K::Float,   20.0f, 20000.0f,   250.0f, 1000.0f,    "Hz", nullptr },
        { "eq_p1_gain",   "Bell 1 Gain",       M::EQ,         K::Float,  -12.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "eq_p1_q",      "Bell 1 Q",          M::EQ,         K::Float,    0.2f,    10.0f,     1.0f,   1.5f,     "",   nullptr },
        { "eq_p2_freq",   "Bell 2 Freq",       M::EQ,         K::Float,   20.0f, 20000.0f,  1000.0f, 1000.0f,    "Hz", nullptr },
        { "eq_p2_gain",   "Bell 2 Gain",       M::EQ,         K::Float,  -12.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "eq_p2_q",      "Bell 2 Q",          M::EQ,         K::Float,    0.2f,    10.0f,     1.0f,   1.5f,     "",   nullptr },
        { "eq_p3_freq",   "Bell 3 Freq",       M::EQ,         K::Float,   20.0f, 20000.0f,  4000.0f, 1000.0f,    "Hz", nullptr },
        { "eq_p3_gain",   "Bell 3 Gain",       M::EQ,         K::Float,  -12.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "eq_p3_q",      "Bell 3 Q",          M::EQ,         K::Float,    0.2f,    10.0f,     1.0f,   1.5f,     "",   nullptr },
        { "eq_hs_freq",   "High Shelf Freq",   M::EQ,         K::Float, 1000.0f, 20000.0f, 10000.0f, 5000.0f,    "Hz", nullptr },
        { "eq_hs_gain",   "High Shelf Gain",   M::EQ,         K::Float,  -12.0f,    12.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "eq_hs_q",      "High Shelf Q",      M::EQ,         K::Float,    0.3f,     2.0f,   0.707f,   0.0f,     "",   nullptr },
        { "eq_lpf_on",    "High Cut On",       M::EQ,         K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "eq_lpf_freq",  "High Cut Freq",     M::EQ,         K::Float, 1000.0f, 22000.0f, 20000.0f, 8000.0f,    "Hz", nullptr },

        { "comp_on",      "Compressor On",     M::Compressor, K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "comp_thresh",  "Comp Threshold",    M::Compressor, K::Float,  -60.0f,     0.0f,   -18.0f,   0.0f,     "dB", nullptr },
        { "comp_ratio",   "Comp Ratio",        M::Compressor, K::Float,    1.0f,    10.0f,     2.0f,   3.0f,     ":1", nullptr },
        { "comp_attack",  "Comp Attack",       M::Compressor, K::Float,    0.1f,   100.0f,    20.0f,  10.0f,     "ms", nullptr },
        { "comp_release", "Comp Release",      M::Compressor, K::Float,   10.0f,  1000.0f,   200.0f, 150.0f,     "ms", nullptr },
        { "comp_knee",    "Comp Knee",         M::Compressor, K::Float,    0.0f,    12.0f,     6.0f,   0.0f,     "dB", nullptr },
        { "comp_makeup",  "Comp Makeup",       M::Compressor, K::Float,  -12.0f,    24.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "comp_mix",     "Comp Mix",          M::Compressor, K::Float,    0.0f,   100.0f,   100.0f,   0.0f,     "%",  nullptr },
        { "comp_sc_hpf",  "Comp Detector HPF", M::Compressor, K::Float,   20.0f,   300.0f,    20.0f,  80.0f,     "Hz", nullptr },

        { "sat_on",       "Saturation On",     M::Saturation, K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "sat_type",     "Saturation Type",   M::Saturation, K::Choice,   0.0f,     2.0f,     0.0f,   0.0f,     "",   "Tape|Tube|Soft Clip" },
        { "sat_drive",    "Saturation Drive",  M::Saturation, K::Float,    0.0f,    24.0f,     6.0f,   0.0f,     "dB", nullptr },
        { "sat_mix",      "Saturation Mix",    M::Saturation, K::Float,    0.0f,   100.0f,    50.0f,   0.0f,     "%",  nullptr },

        { "img_on",       "Imager On",         M::Imager,     K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "img_width",    "Stereo Width",      M::Imager,     K::Float,    0.0f,   200.0f,   100.0f,   0.0f,     "%",  nullptr },
        { "img_mono_on",  "Mono Bass On",      M::Imager,     K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "img_mono_freq","Mono Bass Freq",    M::Imager,     K::Float,   40.0f,   300.0f,   120.0f,   0.0f,     "Hz", nullptr },

        { "lim_on",       "Limiter On",        M::Limiter,    K::Bool,     0.0f,     1.0f,     0.0f,   0.0f,     "",   nullptr },
        { "lim_gain",     "Limiter Gain",      M::Limiter,    K::Float,    0.0f,    24.0f,     0.0f,   0.0f,     "dB", nullptr },
        { "lim_ceiling",  "Limiter Ceiling",   M::Limiter,    K::Float,  -12.0f,     0.0f,    -1.0f,   0.0f,     "dB", nullptr },
        { "lim_release",  "Limiter Release",   M::Limiter,    K::Float,    1.0f,  1000.0f,   100.0f, 100.0f,     "ms", nullptr },
        { "lim_tp",       "True Peak",         M::Limiter,    K::Bool,     0.0f,     1.0f,     1.0f,   0.0f,     "",   nullptr },
    };

    std::string choiceName (const char* choices, int index)
    {
        std::string all (choices != nullptr ? choices : "");
        size_t start = 0;

        for (int i = 0; i < index; ++i)
        {
            const auto bar = all.find ('|', start);
            if (bar == std::string::npos)
                return {};
            start = bar + 1;
        }

        const auto end = all.find ('|', start);
        return all.substr (start, end == std::string::npos ? std::string::npos : end - start);
    }
}

const ParamInfo& paramInfo (int index) noexcept
{
    return table[std::clamp (index, 0, (int) NumParams - 1)];
}

ParamState defaultParamState() noexcept
{
    ParamState s {};
    for (int i = 0; i < NumParams; ++i)
        s[(size_t) i] = table[i].defaultValue;
    return s;
}

int findParam (std::string_view id) noexcept
{
    for (int i = 0; i < NumParams; ++i)
        if (id == table[i].id)
            return i;

    return -1;
}

float clampParam (int index, float value) noexcept
{
    const auto& info = paramInfo (index);
    value = std::clamp (value, info.minValue, info.maxValue);

    if (info.kind != ParamKind::Float)
        value = std::round (value);

    return value;
}

std::string formatParamValue (int index, float value)
{
    const auto& info = paramInfo (index);
    char buf[64];

    if (info.kind == ParamKind::Bool)
        return value >= 0.5f ? "On" : "Off";

    if (info.kind == ParamKind::Choice)
        return choiceName (info.choices, (int) std::lround (value));

    const std::string unit (info.unit);

    if (unit == "Hz")
    {
        if (value >= 1000.0f)
            std::snprintf (buf, sizeof (buf), value >= 10000.0f ? "%.1f kHz" : "%.2f kHz", value / 1000.0f);
        else
            std::snprintf (buf, sizeof (buf), "%.0f Hz", value);
    }
    else if (unit == "dB")
    {
        std::snprintf (buf, sizeof (buf), "%+.1f dB", value);
    }
    else if (unit == "ms")
    {
        std::snprintf (buf, sizeof (buf), value < 10.0f ? "%.1f ms" : "%.0f ms", value);
    }
    else if (unit == "%")
    {
        std::snprintf (buf, sizeof (buf), "%.0f%%", value);
    }
    else if (unit == ":1")
    {
        std::snprintf (buf, sizeof (buf), "%.1f:1", value);
    }
    else
    {
        std::snprintf (buf, sizeof (buf), "%.2f", value);
    }

    return buf;
}

const char* moduleName (Module m) noexcept
{
    switch (m)
    {
        case Module::Global:     return "Global";
        case Module::EQ:         return "Equalizer";
        case Module::Compressor: return "Compressor";
        case Module::Saturation: return "Saturation";
        case Module::Imager:     return "Stereo Imager";
        case Module::Limiter:    return "Limiter";
    }

    return "";
}

int moduleEnableParam (Module m) noexcept
{
    switch (m)
    {
        case Module::EQ:         return pEqOn;
        case Module::Compressor: return pCompOn;
        case Module::Saturation: return pSatOn;
        case Module::Imager:     return pImgOn;
        case Module::Limiter:    return pLimOn;
        case Module::Global:     break;
    }

    return -1;
}

const char* moduleEnableParamId (Module m) noexcept
{
    const int index = moduleEnableParam (m);
    return index >= 0 ? table[index].id : "";
}

} // namespace prizm
