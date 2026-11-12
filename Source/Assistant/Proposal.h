/*
  ==============================================================================

    PRIZM V2 - a set of suggested parameter changes awaiting user approval.

    Nothing the assistant suggests touches the audio until the user applies
    it; every change carries its before/after value and a plain-language
    reason, and can be deselected individually.

  ==============================================================================
*/

#pragma once

#include "../Common/ParamTable.h"

#include <cmath>
#include <string>
#include <vector>

namespace prizm::assistant
{

struct ParamChange
{
    int param = 0;
    float from = 0.0f, to = 0.0f;
    std::string reason;
    bool selected = true;
};

struct Prediction
{
    bool valid = false;
    float integratedLufs = 0.0f;
    float truePeakDb = 0.0f;
    float avgLimiterGrDb = 0.0f, maxLimiterGrDb = 0.0f;
    float avgCompGrDb = 0.0f;
    float plrDb = 0.0f;
    double secondsSimulated = 0.0;
};

struct Proposal
{
    int id = 0;
    std::string title;
    std::string summary;
    std::vector<ParamChange> changes;
    std::vector<std::string> notes;
    Prediction prediction;

    bool empty() const noexcept { return changes.empty(); }

    /** Records a change (clamped). Repeated calls for the same parameter keep
        the original 'from' value and the newest reason. No-ops are dropped. */
    void setChange (const ParamState& current, int param, float to, const std::string& reason)
    {
        to = clampParam (param, to);

        for (auto it = changes.begin(); it != changes.end(); ++it)
        {
            if (it->param == param)
            {
                it->to = to;
                if (! reason.empty())
                    it->reason = reason;
                if (std::abs (it->to - it->from) < 1.0e-4f)
                    changes.erase (it);
                return;
            }
        }

        const float from = current[(size_t) param];
        if (std::abs (to - from) < 1.0e-4f)
            return;

        changes.push_back ({ param, from, to, reason, true });
    }

    /** Builds a proposal from the difference between two complete states. */
    void setFromDiff (const ParamState& current, const ParamState& target, const std::vector<std::string>& reasons)
    {
        for (int p = 0; p < NumParams; ++p)
            if (std::abs (target[(size_t) p] - current[(size_t) p]) > 1.0e-4f)
                setChange (current, p, target[(size_t) p], (size_t) p < reasons.size() ? reasons[(size_t) p] : std::string());
    }

    /** The resulting state if the selected changes are applied to 'current'. */
    ParamState applyTo (const ParamState& current) const
    {
        ParamState s = current;
        for (auto& c : changes)
            if (c.selected)
                s[(size_t) c.param] = c.to;
        return s;
    }

    int numSelected() const noexcept
    {
        int n = 0;
        for (auto& c : changes)
            n += c.selected ? 1 : 0;
        return n;
    }
};

} // namespace prizm::assistant
