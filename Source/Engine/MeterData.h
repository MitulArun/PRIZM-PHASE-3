/*
  ==============================================================================

    PRIZM V2 - real-time measurements shared from the audio thread to the UI.

    Written by the audio thread once per block (relaxed atomics, no locks),
    read by the editor's timer. Every number shown in the meters comes from
    here - nothing in the UI is simulated.

  ==============================================================================
*/

#pragma once

#include <atomic>

namespace prizm
{

struct MeterSnapshot
{
    float inPeak[2] {}, inRms[2] {};        // linear
    float outPeak[2] {}, outRms[2] {};      // linear, processed signal (pre A/B monitor)
    float outTruePeakBlock = 0.0f;          // linear, last block
    float outTruePeakMaxDb = -144.0f;       // since reset

    float inShortTerm = -144.0f, inMomentary = -144.0f, inIntegrated = -144.0f;
    float outMomentary = -144.0f, outShortTerm = -144.0f, outIntegrated = -144.0f, outRange = 0.0f;

    float compGrDb = 0.0f, limGrDb = 0.0f;
    float correlation = 1.0f;               // -1 .. +1, ~300 ms
    float originalMatchDb = 0.0f;           // level-match gain applied to the original in A/B
    bool audioActive = false;               // something above -70 dBFS in the last ~0.5 s
};

class MeterData
{
public:
    void store (const MeterSnapshot& s) noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            inPeak[ch].store (s.inPeak[ch], std::memory_order_relaxed);
            inRms[ch].store (s.inRms[ch], std::memory_order_relaxed);
            outPeak[ch].store (s.outPeak[ch], std::memory_order_relaxed);
            outRms[ch].store (s.outRms[ch], std::memory_order_relaxed);
        }

        outTruePeakBlock.store (s.outTruePeakBlock, std::memory_order_relaxed);
        outTruePeakMaxDb.store (s.outTruePeakMaxDb, std::memory_order_relaxed);
        inShortTerm.store (s.inShortTerm, std::memory_order_relaxed);
        inMomentary.store (s.inMomentary, std::memory_order_relaxed);
        inIntegrated.store (s.inIntegrated, std::memory_order_relaxed);
        outMomentary.store (s.outMomentary, std::memory_order_relaxed);
        outShortTerm.store (s.outShortTerm, std::memory_order_relaxed);
        outIntegrated.store (s.outIntegrated, std::memory_order_relaxed);
        outRange.store (s.outRange, std::memory_order_relaxed);
        compGrDb.store (s.compGrDb, std::memory_order_relaxed);
        limGrDb.store (s.limGrDb, std::memory_order_relaxed);
        correlation.store (s.correlation, std::memory_order_relaxed);
        originalMatchDb.store (s.originalMatchDb, std::memory_order_relaxed);
        audioActive.store (s.audioActive, std::memory_order_relaxed);
    }

    MeterSnapshot load() const noexcept
    {
        MeterSnapshot s;
        for (int ch = 0; ch < 2; ++ch)
        {
            s.inPeak[ch] = inPeak[ch].load (std::memory_order_relaxed);
            s.inRms[ch] = inRms[ch].load (std::memory_order_relaxed);
            s.outPeak[ch] = outPeak[ch].load (std::memory_order_relaxed);
            s.outRms[ch] = outRms[ch].load (std::memory_order_relaxed);
        }

        s.outTruePeakBlock = outTruePeakBlock.load (std::memory_order_relaxed);
        s.outTruePeakMaxDb = outTruePeakMaxDb.load (std::memory_order_relaxed);
        s.inShortTerm = inShortTerm.load (std::memory_order_relaxed);
        s.inMomentary = inMomentary.load (std::memory_order_relaxed);
        s.inIntegrated = inIntegrated.load (std::memory_order_relaxed);
        s.outMomentary = outMomentary.load (std::memory_order_relaxed);
        s.outShortTerm = outShortTerm.load (std::memory_order_relaxed);
        s.outIntegrated = outIntegrated.load (std::memory_order_relaxed);
        s.outRange = outRange.load (std::memory_order_relaxed);
        s.compGrDb = compGrDb.load (std::memory_order_relaxed);
        s.limGrDb = limGrDb.load (std::memory_order_relaxed);
        s.correlation = correlation.load (std::memory_order_relaxed);
        s.originalMatchDb = originalMatchDb.load (std::memory_order_relaxed);
        s.audioActive = audioActive.load (std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<float> inPeak[2] {}, inRms[2] {}, outPeak[2] {}, outRms[2] {};
    std::atomic<float> outTruePeakBlock { 0.0f }, outTruePeakMaxDb { -144.0f };
    std::atomic<float> inShortTerm { -144.0f }, inMomentary { -144.0f }, inIntegrated { -144.0f };
    std::atomic<float> outMomentary { -144.0f }, outShortTerm { -144.0f }, outIntegrated { -144.0f }, outRange { 0.0f };
    std::atomic<float> compGrDb { 0.0f }, limGrDb { 0.0f }, correlation { 1.0f }, originalMatchDb { 0.0f };
    std::atomic<bool> audioActive { false };
};

} // namespace prizm
