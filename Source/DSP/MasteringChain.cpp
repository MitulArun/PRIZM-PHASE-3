/*
  ==============================================================================

    PRIZM V2 - the complete mastering chain.

  ==============================================================================
*/

#include "MasteringChain.h"

namespace prizm::dsp
{

EQSettings makeEQSettings (const ParamState& p) noexcept
{
    EQSettings eq;
    eq.enabled = isOn (p, pEqOn);
    eq.hpfOn = isOn (p, pEqHpfOn);
    eq.hpfFreq = p[pEqHpfFreq];
    eq.lsFreq = p[pEqLsFreq];
    eq.lsGain = p[pEqLsGain];
    eq.lsQ = p[pEqLsQ];

    const int freqs[3] = { pEqP1Freq, pEqP2Freq, pEqP3Freq };
    for (int i = 0; i < 3; ++i)
    {
        eq.pFreq[i] = p[(size_t) freqs[i]];
        eq.pGain[i] = p[(size_t) freqs[i] + 1];
        eq.pQ[i] = p[(size_t) freqs[i] + 2];
    }

    eq.hsFreq = p[pEqHsFreq];
    eq.hsGain = p[pEqHsGain];
    eq.hsQ = p[pEqHsQ];
    eq.lpfOn = isOn (p, pEqLpfOn);
    eq.lpfFreq = p[pEqLpfFreq];
    return eq;
}

ChainSettings makeChainSettings (const ParamState& p) noexcept
{
    ChainSettings s;
    s.inGainDb = p[pInGain];
    s.outGainDb = p[pOutGain];
    s.eq = makeEQSettings (p);

    s.comp.enabled = isOn (p, pCompOn);
    s.comp.thresholdDb = p[pCompThresh];
    s.comp.ratio = p[pCompRatio];
    s.comp.attackMs = p[pCompAttack];
    s.comp.releaseMs = p[pCompRelease];
    s.comp.kneeDb = p[pCompKnee];
    s.comp.makeupDb = p[pCompMakeup];
    s.comp.mixPercent = p[pCompMix];
    s.comp.detectorHpfHz = p[pCompScHpf];

    s.sat.enabled = isOn (p, pSatOn);
    s.sat.type = (int) std::lround (p[pSatType]);
    s.sat.driveDb = p[pSatDrive];
    s.sat.mixPercent = p[pSatMix];

    s.img.enabled = isOn (p, pImgOn);
    s.img.widthPercent = p[pImgWidth];
    s.img.monoBassOn = isOn (p, pImgMonoOn);
    s.img.monoBassHz = p[pImgMonoFreq];

    s.lim.enabled = isOn (p, pLimOn);
    s.lim.gainDb = p[pLimGain];
    s.lim.ceilingDb = p[pLimCeiling];
    s.lim.releaseMs = p[pLimRelease];
    s.lim.truePeak = isOn (p, pLimTruePeak);
    return s;
}

//==============================================================================
void MasteringChain::prepare (double sampleRate)
{
    fs = sampleRate;
    eq.prepare (fs);
    compressor.prepare (fs);
    saturator.prepare (fs, chunkSize);
    imager.prepare (fs);
    limiter.prepare (fs);

    inGain.prepare (fs, 0.05);
    outGain.prepare (fs, 0.05);
    originalMix.prepare (fs, 0.03);     // A/B crossfade
    originalGain.prepare (fs, 0.2);
    monoMix.prepare (fs, 0.03);

    const int latency = getLatencySamples();
    for (auto& d : originalDelay)
    {
        d.prepare (latency);
        d.setDelay (latency);
    }

    inGain.reset (1.0f);
    outGain.reset (1.0f);
    originalMix.reset (0.0f);
    originalGain.reset (1.0f);
    monoMix.reset (0.0f);
}

void MasteringChain::setSettings (const ChainSettings& s) noexcept
{
    inGain.setTarget (dbToGain (s.inGainDb));
    outGain.setTarget (dbToGain (s.outGainDb));
    eq.setSettings (s.eq);
    compressor.setSettings (s.comp);
    saturator.setSettings (s.sat);
    imager.setSettings (s.img);
    limiter.setSettings (s.lim);
}

void MasteringChain::reset (const ChainSettings& s) noexcept
{
    inGain.reset (dbToGain (s.inGainDb));
    outGain.reset (dbToGain (s.outGainDb));
    eq.reset (s.eq);
    compressor.reset (s.comp);
    saturator.reset (s.sat);
    imager.reset (s.img);
    limiter.reset (s.lim);

    for (auto& d : originalDelay)
        d.clear();
}

void MasteringChain::setMonitor (const MonitorSettings& m) noexcept
{
    originalMix.setTarget (m.listenToOriginal ? 1.0f : 0.0f);
    originalGain.setTarget (dbToGain (clampValue (m.originalGainDb, -24.0f, 24.0f)));
    monoMix.setTarget (m.monoCheck ? 1.0f : 0.0f);
}

void MasteringChain::process (float* left, float* right, int numSamples, float* tapLeft, float* tapRight) noexcept
{
    for (int start = 0; start < numSamples; start += chunkSize)
        processChunk (left + start, right + start, std::min (chunkSize, numSamples - start), true,
                      tapLeft != nullptr ? tapLeft + start : nullptr,
                      tapRight != nullptr ? tapRight + start : nullptr);
}

void MasteringChain::processPreLimiter (float* left, float* right, int numSamples) noexcept
{
    for (int start = 0; start < numSamples; start += chunkSize)
        processChunk (left + start, right + start, std::min (chunkSize, numSamples - start), false);
}

void MasteringChain::processChunk (float* left, float* right, int n, bool includeLimiter,
                                   float* tapLeft, float* tapRight) noexcept
{
    std::copy (left, left + n, dryL);
    std::copy (right, right + n, dryR);

    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.next();
        left[i] *= g;
        right[i] *= g;
    }

    eq.process (left, right, n);
    compressor.process (left, right, n);
    saturator.process (left, right, n);
    imager.process (left, right, n);

    if (! includeLimiter)
        return;

    limiter.process (left, right, n);

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.next();
        const float a = originalMix.next();
        const float og = originalGain.next();
        const float mono = monoMix.next();

        const float origL = originalDelay[0].process (dryL[i]) * og;
        const float origR = originalDelay[1].process (dryR[i]) * og;

        float l = left[i] * g, r = right[i] * g;

        if (tapLeft != nullptr)
        {
            tapLeft[i] = l;
            tapRight[i] = r;
        }

        l += (origL - l) * a;
        r += (origR - r) * a;

        const float m = 0.5f * (l + r);
        left[i]  = l + (m - l) * mono;
        right[i] = r + (m - r) * mono;
    }
}

} // namespace prizm::dsp
