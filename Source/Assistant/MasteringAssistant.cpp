/*
  ==============================================================================

    PRIZM V2 - mastering assistant: analysis-driven chain proposals.

  ==============================================================================
*/

#include "MasteringAssistant.h"

#include <cstdarg>
#include <cstdio>

namespace prizm::assistant
{

using dsp::AnalysisResult;
constexpr int numPoints = AnalysisResult::numPoints;
using Curve = std::array<float, numPoints>;

namespace
{
    std::string fmt (const char* format, ...)
    {
        char buf[512];
        va_list args;
        va_start (args, format);
        std::vsnprintf (buf, sizeof (buf), format, args);
        va_end (args);
        return buf;
    }

    float pointFreq (int k) noexcept { return AnalysisResult::pointFrequency (k); }

    Curve smoothCurve (const Curve& c, int radius)
    {
        Curve out {};
        for (int k = 0; k < numPoints; ++k)
        {
            double sum = 0.0;
            int n = 0;
            for (int j = std::max (0, k - radius); j <= std::min (numPoints - 1, k + radius); ++j)
            {
                sum += c[(size_t) j];
                ++n;
            }
            out[(size_t) k] = (float) (sum / n);
        }
        return out;
    }

    float curveAt (const Curve& c, float hz)
    {
        const float pos = std::log (std::max (20.0f, hz) / 20.0f) / std::log (1000.0f) * (numPoints - 1);
        const int i = dsp::clampValue ((int) std::floor (pos), 0, numPoints - 2);
        const float frac = dsp::clampValue (pos - i, 0.0f, 1.0f);
        return c[(size_t) i] + (c[(size_t) i + 1] - c[(size_t) i]) * frac;
    }

    float averageOver (const Curve& c, float loHz, float hiHz)
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < numPoints; ++k)
            if (pointFreq (k) >= loHz && pointFreq (k) <= hiHz)
            {
                sum += c[(size_t) k];
                ++n;
            }
        return n > 0 ? (float) (sum / n) : 0.0f;
    }

    /** Residual of a spectrum relative to its own straight-line fit (100 Hz - 10 kHz). */
    Curve residualFromLine (const Curve& normalised, float slope)
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < numPoints; ++k)
        {
            const float f = pointFreq (k);
            if (f >= 100.0f && f <= 10000.0f)
            {
                sum += normalised[(size_t) k] - slope * std::log2 (f);
                ++n;
            }
        }

        const float intercept = n > 0 ? (float) (sum / n) : 0.0f;
        Curve r {};
        for (int k = 0; k < numPoints; ++k)
            r[(size_t) k] = normalised[(size_t) k] - (intercept + slope * std::log2 (pointFreq (k)));
        return r;
    }

    /** Correction curve with no reference: steer the tilt into the typical
        range and gently flatten pronounced resonances / dips. */
    Curve genericCorrection (const AnalysisResult& in, std::string& tiltReason)
    {
        // Only steer mixes that are clearly outside the typical range, and only
        // back to just inside it (0.5 dB/oct) - not all the way to the centre.
        const float s = in.tiltDbPerOctave;
        float deltaSlope = 0.0f;

        if (s < -6.5f)
        {
            deltaSlope = std::min (1.5f, (-6.5f - s) + 0.5f);
            tiltReason = fmt ("your mix leans dark (%.1f dB/oct vs ~%.0f dB/oct typical of commercial masters)", s, typicalTiltDbPerOctave);
        }
        else if (s > -3.5f)
        {
            deltaSlope = std::max (-1.5f, (-3.5f - s) - 0.5f);
            tiltReason = fmt ("your mix leans bright (%.1f dB/oct vs ~%.0f dB/oct typical of commercial masters)", s, typicalTiltDbPerOctave);
        }

        const auto normalised = in.normalisedMid();
        const auto residual = residualFromLine (smoothCurve (normalised, 3), s);

        Curve d {};
        for (int k = 0; k < numPoints; ++k)
        {
            const float f = pointFreq (k);
            const float lf = std::log2 (dsp::clampValue (f, 80.0f, 12000.0f) / 1000.0f);
            float v = deltaSlope * lf * 0.6f;

            if (f >= 150.0f && f <= 8000.0f)
            {
                const float r = residual[(size_t) k];
                const float excess = std::max (0.0f, std::abs (r) - 2.0f);
                v -= (r > 0.0f ? 1.0f : -1.0f) * excess * 0.5f;
            }

            d[(size_t) k] = dsp::clampValue (v, -4.0f, 4.0f);
        }

        return smoothCurve (d, 2);
    }

    Curve referenceCorrection (const AnalysisResult& in, const AnalysisResult& ref, float amount)
    {
        const auto a = in.normalisedMid(), b = ref.normalisedMid();
        Curve d {};
        for (int k = 0; k < numPoints; ++k)
            d[(size_t) k] = b[(size_t) k] - a[(size_t) k];

        d = smoothCurve (d, 2);

        for (int k = 0; k < numPoints; ++k)
        {
            const float f = pointFreq (k);
            float w = 1.0f;
            if (f < 30.0f)        w = 0.0f;
            else if (f < 40.0f)   w = (f - 30.0f) / 10.0f;
            else if (f > 16000.0f) w = 0.0f;
            else if (f > 12000.0f) w = (16000.0f - f) / 4000.0f;
            d[(size_t) k] = dsp::clampValue (d[(size_t) k] * amount * w, -5.0f, 5.0f);
        }

        return d;
    }

    void addStyleNudges (Curve& d, const MasterRequest& r)
    {
        for (int k = 0; k < numPoints; ++k)
        {
            const float f = pointFreq (k);
            const float low = 1.0f / (1.0f + (f / 200.0f) * (f / 200.0f));
            const float high = 1.0f / (1.0f + (5000.0f / f) * (5000.0f / f));
            d[(size_t) k] += 1.5f * r.warmth * low + 1.5f * r.brightness * high;
        }
    }

    //==========================================================================
    struct BlockStats
    {
        std::vector<float> rmsDb;    // input RMS per 480-sample block
        float loudGate = -60.0f;     // blocks above this count as "loud parts"
    };

    constexpr int simBlock = 480;

    BlockStats computeBlockStats (const AudioClip& clip)
    {
        BlockStats st;
        for (int i = 0; i < clip.numSamples; i += simBlock)
        {
            const int n = std::min (simBlock, clip.numSamples - i);
            double s = 0.0;
            for (int j = 0; j < n; ++j)
                s += (double) clip.left[i + j] * clip.left[i + j] + (double) clip.right[i + j] * clip.right[i + j];
            st.rmsDb.push_back ((float) dsp::powerToDb (s / (2.0 * n)));
        }

        auto sorted = st.rmsDb;
        std::sort (sorted.begin(), sorted.end());
        if (! sorted.empty())
            st.loudGate = std::max (-60.0f, sorted[sorted.size() / 2]);   // median: "the loud half"
        return st;
    }

    /** Average compressor gain reduction on the loud half of the clip. */
    float measureCompressorGr (const ParamState& params, const AudioClip& clip, const BlockStats& st)
    {
        ParamState p = params;
        p[pSatOn] = 0.0f;
        p[pImgOn] = 0.0f;
        p[pLimOn] = 0.0f;

        dsp::MasteringChain chain;
        chain.prepare (clip.sampleRate);
        chain.reset (dsp::makeChainSettings (p));

        std::vector<float> l (clip.left, clip.left + clip.numSamples), r (clip.right, clip.right + clip.numSamples);
        double sum = 0.0;
        int n = 0;

        for (int i = 0, b = 0; i < clip.numSamples; i += simBlock, ++b)
        {
            const int len = std::min (simBlock, clip.numSamples - i);
            chain.processPreLimiter (l.data() + i, r.data() + i, len);
            if (st.rmsDb[(size_t) b] >= st.loudGate)
            {
                sum += chain.getCompressorGainReductionDb();
                ++n;
            }
        }

        return n > 0 ? (float) (sum / n) : 0.0f;
    }

    struct LimiterResult
    {
        float integrated = dsp::LoudnessMeter::silence;
        float truePeakDb = -144.0f;
        float avgGr = 0.0f, maxGr = 0.0f;
    };

    LimiterResult runLimiter (const std::vector<float>& preL, const std::vector<float>& preR, double fs,
                              const dsp::LimiterSettings& settings, const BlockStats& st)
    {
        dsp::Limiter lim;
        lim.prepare (fs);
        lim.reset (settings);

        dsp::LoudnessMeter meter;
        meter.prepare (fs);
        dsp::TruePeakDetector tpL, tpR;

        std::vector<float> l = preL, r = preR;
        LimiterResult res;
        double grSum = 0.0;
        int grCount = 0;
        float tp = 0.0f;

        for (int i = 0, b = 0; i < (int) l.size(); i += simBlock, ++b)
        {
            const int len = std::min (simBlock, (int) l.size() - i);
            lim.process (l.data() + i, r.data() + i, len);
            meter.process (l.data() + i, r.data() + i, len);

            for (int j = 0; j < len; ++j)
                tp = std::max (tp, std::max (tpL.process (l[(size_t) (i + j)]), tpR.process (r[(size_t) (i + j)])));

            const float gr = lim.getGainReductionDb();
            res.maxGr = std::max (res.maxGr, gr);
            if ((size_t) b < st.rmsDb.size() && st.rmsDb[(size_t) b] >= st.loudGate)
            {
                grSum += gr;
                ++grCount;
            }
        }

        res.integrated = meter.getIntegrated();
        res.truePeakDb = dsp::gainToDb (tp, -144.0f);
        res.avgGr = grCount > 0 ? (float) (grSum / grCount) : 0.0f;
        return res;
    }

    const char* tiltWord (float tilt)
    {
        if (tilt < -6.5f) return "dark";
        if (tilt > -3.5f) return "bright";
        return "balanced";
    }
}

//==============================================================================
void fitEqToCurve (const Curve& curve, double fs, float maxGainDb, ParamState& params)
{
    dsp::EQSettings eq = dsp::makeEQSettings (params);
    eq.enabled = true;
    eq.lsFreq = 110.0f;  eq.lsQ = 0.707f;  eq.lsGain = 0.0f;
    eq.hsFreq = 9000.0f; eq.hsQ = 0.707f;  eq.hsGain = 0.0f;

    const float regions[3][2] = { { 150.0f, 600.0f }, { 600.0f, 2500.0f }, { 2500.0f, 8000.0f } };
    const float initial[3] = { 300.0f, 1200.0f, 4500.0f };
    for (int b = 0; b < 3; ++b)
    {
        eq.pFreq[b] = initial[b];
        eq.pGain[b] = 0.0f;
        eq.pQ[b] = 0.9f;
    }

    // Shelves/bells must not interact with the user's filters during fitting.
    eq.hpfOn = eq.lpfOn = false;

    auto residualAt = [&] (int k) { return curve[(size_t) k] - (float) dsp::ParametricEQ::responseDb (eq, fs, pointFreq (k)); };
    auto averageResidual = [&] (float lo, float hi)
    {
        double s = 0.0;
        int n = 0;
        for (int k = 0; k < numPoints; ++k)
            if (pointFreq (k) >= lo && pointFreq (k) <= hi) { s += residualAt (k); ++n; }
        return n > 0 ? (float) (s / n) : 0.0f;
    };

    for (int iter = 0; iter < 40; ++iter)
    {
        if (iter == 0 || iter == 12)
        {
            for (int b = 0; b < 3; ++b)
            {
                float best = 0.0f;
                for (int k = 0; k < numPoints; ++k)
                {
                    const float f = pointFreq (k);
                    if (f < regions[b][0] || f > regions[b][1])
                        continue;
                    const float r = residualAt (k);
                    if (std::abs (r) > std::abs (best))
                    {
                        best = r;
                        eq.pFreq[b] = f;
                    }
                }
            }
        }

        eq.lsGain = dsp::clampValue (eq.lsGain + 0.5f * averageResidual (30.0f, 90.0f), -maxGainDb, maxGainDb);
        eq.hsGain = dsp::clampValue (eq.hsGain + 0.5f * averageResidual (10000.0f, 16000.0f), -maxGainDb, maxGainDb);

        for (int b = 0; b < 3; ++b)
        {
            const float r = averageResidual (eq.pFreq[b] / 1.15f, eq.pFreq[b] * 1.15f);
            eq.pGain[b] = dsp::clampValue (eq.pGain[b] + 0.5f * r, -maxGainDb, maxGainDb);
        }
    }

    auto tidy = [] (float g) { return std::abs (g) < 0.25f ? 0.0f : std::round (g * 10.0f) / 10.0f; };

    params[pEqLsFreq] = eq.lsFreq;
    params[pEqLsGain] = tidy (eq.lsGain);
    params[pEqLsQ] = eq.lsQ;
    params[pEqHsFreq] = eq.hsFreq;
    params[pEqHsGain] = tidy (eq.hsGain);
    params[pEqHsQ] = eq.hsQ;

    const int base[3] = { pEqP1Freq, pEqP2Freq, pEqP3Freq };
    for (int b = 0; b < 3; ++b)
    {
        params[(size_t) base[b]] = std::round (eq.pFreq[b]);
        params[(size_t) base[b] + 1] = tidy (eq.pGain[b]);
        params[(size_t) base[b] + 2] = eq.pQ[b];
    }

    params[pEqOn] = 1.0f;
}

//==============================================================================
Prediction simulate (const ParamState& params, const AudioClip& clip)
{
    Prediction pr;
    if (clip.numSamples <= 0)
        return pr;

    const auto st = computeBlockStats (clip);

    dsp::MasteringChain chain;
    chain.prepare (clip.sampleRate);
    chain.reset (dsp::makeChainSettings (params));

    std::vector<float> l (clip.left, clip.left + clip.numSamples), r (clip.right, clip.right + clip.numSamples);
    dsp::LoudnessMeter meter;
    meter.prepare (clip.sampleRate);
    dsp::TruePeakDetector tpL, tpR;
    float tp = 0.0f;
    double limSum = 0.0, compSum = 0.0;
    int n = 0;

    for (int i = 0, b = 0; i < clip.numSamples; i += simBlock, ++b)
    {
        const int len = std::min (simBlock, clip.numSamples - i);
        chain.process (l.data() + i, r.data() + i, len);
        meter.process (l.data() + i, r.data() + i, len);
        for (int j = 0; j < len; ++j)
            tp = std::max (tp, std::max (tpL.process (l[(size_t) (i + j)]), tpR.process (r[(size_t) (i + j)])));

        pr.maxLimiterGrDb = std::max (pr.maxLimiterGrDb, chain.getLimiterGainReductionDb());
        if (st.rmsDb[(size_t) b] >= st.loudGate)
        {
            limSum += chain.getLimiterGainReductionDb();
            compSum += chain.getCompressorGainReductionDb();
            ++n;
        }
    }

    pr.valid = meter.getIntegrated() > dsp::LoudnessMeter::silence;
    pr.integratedLufs = meter.getIntegrated();
    pr.truePeakDb = dsp::gainToDb (tp, -144.0f);
    pr.avgLimiterGrDb = n > 0 ? (float) (limSum / n) : 0.0f;
    pr.avgCompGrDb = n > 0 ? (float) (compSum / n) : 0.0f;
    pr.plrDb = pr.truePeakDb - pr.integratedLufs;
    pr.secondsSimulated = clip.seconds();
    return pr;
}

//==============================================================================
Proposal buildMasteringProposal (const MasterRequest& request, const ParamState& current,
                                 const AnalysisResult& input, const AnalysisResult* reference,
                                 const AudioClip& clip, const CancelCheck& cancelled)
{
    Proposal proposal;
    ParamState p = current;
    std::vector<std::string> reasons ((size_t) NumParams);
    auto reason = [&reasons] (std::initializer_list<int> params, const std::string& text)
    {
        for (int i : params)
            reasons[(size_t) i] = text;
    };

    const bool useRef = request.matchReference && reference != nullptr && reference->valid;
    const float targetLufs = useRef ? reference->integratedLufs : request.targetLufs;
    const float ceiling = useRef ? std::min (-0.3f, std::max (-2.0f, std::round (reference->truePeakDb * 10.0f) / 10.0f))
                                 : request.ceilingDb;
    const auto stats = computeBlockStats (clip);

    if (useRef)
        proposal.title = "Match reference";
    else if (request.loudnessOnly)
        proposal.title = fmt ("Loudness to %.1f LUFS", targetLufs);
    else
        proposal.title = request.platform.empty() ? fmt ("Mastering chain (%.0f LUFS)", targetLufs)
                                                  : fmt ("Mastering chain for %s (%.0f LUFS)", request.platform.c_str(), targetLufs);

    proposal.notes.push_back (fmt ("Based on %.1f s of your track that PRIZM heard (input, before processing).", clip.seconds()));

    if (! request.loudnessOnly)
    {
        //----------------------------------------------------------------------
        // Gain staging: a very quiet mix gets clean input gain first, so the
        // compressor and saturation see a sensible level and the limiter only
        // limits instead of acting as a huge make-up gain.
        const float stagedPeak = input.truePeakDb + p[pInGain];
        if (stagedPeak < -9.0f)
        {
            // Never stage past the loudness target itself (matters for quiet targets / references).
            const float maxTrim = p[pInGain] + (targetLufs - input.integratedLufs);
            const float trim = std::min ({ 24.0f, maxTrim, std::round ((p[pInGain] + (-6.0f - stagedPeak)) * 2.0f) / 2.0f });
            if (trim > p[pInGain] + 0.4f)
            {
                p[pInGain] = trim;
                reason ({ pInGain }, fmt ("Gain staging: your mix peaks at %.1f dBTP, so %+.1f dB of clean gain brings it to ~-6 dBTP before processing",
                                          input.truePeakDb, trim));
            }
        }

        //----------------------------------------------------------------------
        // Output trim: the limiter ceiling should be the final word on peaks.
        if (std::abs (p[pOutGain]) > 0.05f)
        {
            p[pOutGain] = 0.0f;
            reason ({ pOutGain }, "Output trim reset so the limiter ceiling sets the final peak level");
        }

        //----------------------------------------------------------------------
        // EQ
        std::string tiltReason;
        Curve correction = useRef ? referenceCorrection (input, *reference, 0.6f) : genericCorrection (input, tiltReason);
        addStyleNudges (correction, request);

        ParamState eqFit = p;
        fitEqToCurve (correction, clip.sampleRate, useRef ? 5.0f : 4.0f, eqFit);

        const int gainParams[5] = { pEqLsGain, pEqP1Gain, pEqP2Gain, pEqP3Gain, pEqHsGain };
        const int freqParams[5] = { pEqLsFreq, pEqP1Freq, pEqP2Freq, pEqP3Freq, pEqHsFreq };
        bool anyEq = false;

        for (int b = 0; b < 5; ++b)
        {
            const int gp = gainParams[b], fp = freqParams[b];
            p[(size_t) fp] = eqFit[(size_t) fp];
            p[(size_t) gp] = eqFit[(size_t) gp];

            const float gain = eqFit[(size_t) gp];
            const float freq = eqFit[(size_t) fp];
            const float wanted = curveAt (correction, b == 0 ? 60.0f : (b == 4 ? 12000.0f : freq));

            std::string why;
            if (std::abs (gain) < 0.05f)
                why = "Not needed - this region already matches the target";
            else if (useRef)
                why = fmt ("The reference has %s energy around %s (%+.1f dB)", wanted > 0 ? "more" : "less",
                           formatParamValue (fp, freq).c_str(), wanted / 0.6f);
            else if (! tiltReason.empty() && (b == 0 || b == 4))
                why = fmt ("%s %s: %s", gain > 0 ? "Lift" : "Ease", b == 0 ? "the lows" : "the highs", tiltReason.c_str());
            else
                why = fmt ("Smooths a %s around %s", gain < 0 ? "build-up" : "dip", formatParamValue (fp, freq).c_str());

            reason ({ gp, fp }, why);
            anyEq = anyEq || std::abs (gain) >= 0.05f;
        }

        // Q values come from the fit as well.
        p[pEqLsQ] = eqFit[pEqLsQ];
        p[pEqHsQ] = eqFit[pEqHsQ];
        p[pEqP1Q] = eqFit[pEqP1Q];
        p[pEqP2Q] = eqFit[pEqP2Q];
        p[pEqP3Q] = eqFit[pEqP3Q];
        reason ({ pEqLsQ, pEqHsQ, pEqP1Q, pEqP2Q, pEqP3Q }, "Broad, musical band shape for mastering");

        // Bands that stay flat keep the user's frequency/Q (avoid noise in the proposal).
        for (int b = 0; b < 5; ++b)
        {
            const int gp = gainParams[b], fp = freqParams[b];
            const int qp = gp + 1;   // every gain band is laid out as freq, gain, Q
            if (std::abs (p[(size_t) gp]) < 0.05f && std::abs (current[(size_t) gp]) < 0.05f)
            {
                p[(size_t) gp] = current[(size_t) gp];
                p[(size_t) fp] = current[(size_t) fp];
                p[(size_t) qp] = current[(size_t) qp];
            }
        }

        if (anyEq)
        {
            p[pEqOn] = 1.0f;
            reason ({ pEqOn }, useRef ? "Tonal balance match towards the reference" : "Gentle tonal balance correction");
        }

        // Subsonic rumble: lots of energy below ~30 Hz relative to 50-60 Hz.
        const float rumble = averageOver (input.midDb, 20.0f, 28.0f) - averageOver (input.midDb, 45.0f, 65.0f);
        if (rumble > -3.0f && ! isOn (p, pEqHpfOn))
        {
            p[pEqHpfOn] = 1.0f;
            p[pEqHpfFreq] = 22.0f;
            reason ({ pEqHpfOn, pEqHpfFreq }, "Strong sub-30 Hz rumble eats limiter headroom without being audible");
        }

        if (cancelled && cancelled())
            return {};

        //----------------------------------------------------------------------
        // Compressor
        const float targetPlr = useRef ? reference->plrDb : (targetLufs >= -11.0f ? 8.5f : 11.0f);
        const bool dense = input.plrDb < 9.0f && input.loudnessRange < 6.0f;
        const bool wantsComp = request.punch > 0.0f
                               || input.loudnessRange > 7.0f
                               || input.plrDb > targetPlr + 4.0f;

        if (dense && request.punch <= 0.0f)
        {
            if (isOn (p, pCompOn))
            {
                p[pCompOn] = 0.0f;
                reason ({ pCompOn }, "Your track is already dense; extra compression would flatten it further");
            }
            proposal.notes.push_back (fmt ("No compression added: the track is already dense (PLR %.1f dB, LRA %.1f LU).",
                                           input.plrDb, input.loudnessRange));
        }
        else if (wantsComp)
        {
            const bool punchy = request.punch > 0.0f;
            const bool bassHeavy = input.bandDb[0] > -9.0f || input.bandDb[1] > -3.0f;

            p[pCompOn] = 1.0f;
            p[pCompRatio] = punchy ? 2.5f : (input.loudnessRange > 10.0f ? 2.0f : 1.6f);
            p[pCompAttack] = punchy ? 25.0f : 30.0f;
            p[pCompRelease] = punchy ? 120.0f : 200.0f;
            p[pCompKnee] = 6.0f;
            p[pCompMix] = 100.0f;
            p[pCompScHpf] = bassHeavy ? 90.0f : 60.0f;

            const float targetGr = punchy ? 2.5f : (input.loudnessRange > 10.0f ? 2.0f : 1.5f);
            float lo = -45.0f, hi = -3.0f;
            for (int it = 0; it < 8; ++it)
            {
                if (cancelled && cancelled())
                    return {};
                const float mid = 0.5f * (lo + hi);
                p[pCompThresh] = mid;
                const float gr = measureCompressorGr (p, clip, stats);
                if (gr > targetGr) lo = mid; else hi = mid;
            }

            p[pCompThresh] = std::round (0.5f * (lo + hi) * 2.0f) / 2.0f;
            const float gr = measureCompressorGr (p, clip, stats);
            p[pCompMakeup] = std::round (gr * 2.0f) / 2.0f;

            const std::string why = punchy
                ? fmt ("Punch: slower 25 ms attack lets transients through while ~%.1f dB of gain reduction thickens the body", gr)
                : fmt ("Glue: loudness range %.1f LU / PLR %.1f dB - gentle %.1f:1 compression (~%.1f dB on loud parts)",
                       input.loudnessRange, input.plrDb, p[pCompRatio], gr);
            reason ({ pCompOn, pCompRatio, pCompThresh, pCompAttack, pCompRelease, pCompKnee, pCompMix }, why);
            reason ({ pCompMakeup }, "Makeup gain keeps the compressor level-neutral so A/B comparisons are fair");
            reason ({ pCompScHpf }, bassHeavy ? "Detector ignores the deep bass so the kick/808 doesn't pump the whole mix"
                                              : "Detector ignores sub-bass to avoid pumping");
        }

        //----------------------------------------------------------------------
        // Saturation
        const bool loudTarget = targetLufs >= -11.0f;
        if (request.saturation > 0.0f || (loudTarget && input.plrDb > 12.0f))
        {
            const float amount = request.saturation > 0.0f ? request.saturation : 0.6f;
            p[pSatOn] = 1.0f;
            p[pSatType] = 0.0f;
            p[pSatDrive] = std::round ((3.0f + 4.0f * amount) * 2.0f) / 2.0f;
            p[pSatMix] = std::round (25.0f + 20.0f * amount);
            reason ({ pSatOn, pSatType, pSatDrive, pSatMix }, request.saturation > 0.0f
                        ? "Tape-style harmonics for the warmth / analog colour you asked for"
                        : "Tape saturation rounds sharp transients so the limiter has less to catch at a loud target");
        }

        //----------------------------------------------------------------------
        // Stereo imager
        bool imagerOn = false;
        if (input.lowSideToMidDb > -12.0f)
        {
            p[pImgMonoOn] = 1.0f;
            p[pImgMonoFreq] = 120.0f;
            imagerOn = true;
            reason ({ pImgMonoOn, pImgMonoFreq }, fmt ("Low end has side content (%.1f dB vs mid below 150 Hz); mono bass keeps it solid on phones, clubs and vinyl",
                                                       input.lowSideToMidDb));
        }

        float width = p[pImgWidth];
        if (input.correlation < 0.15f)
        {
            width = std::min (width, 85.0f);
            proposal.notes.push_back (fmt ("Stereo correlation is low (%.2f): part of the mix may cancel in mono. Width reduced; check with Mono.",
                                           input.correlation));
        }
        else if (useRef)
        {
            const float diff = reference->sideToMidDb - input.sideToMidDb;
            if (std::abs (diff) > 1.5f && ! (diff > 0.0f && input.correlation < 0.3f))
                width = dsp::clampValue (100.0f * dsp::dbToGain (diff * 0.7f), 80.0f, 125.0f);
        }
        else if (input.sideToMidDb < -20.0f && input.correlation > 0.9f)
        {
            width = 112.0f;
        }

        if (request.width != 0.0f)
            width = dsp::clampValue (width + 15.0f * request.width, 60.0f, 150.0f);

        width = std::round (width);
        if (std::abs (width - 100.0f) >= 1.0f)
        {
            p[pImgWidth] = width;
            imagerOn = true;
            reason ({ pImgWidth }, useRef ? fmt ("Stereo width towards the reference (side/mid %.1f dB vs %.1f dB)", input.sideToMidDb, reference->sideToMidDb)
                                          : (width < 100.0f ? "Narrows the image to protect mono compatibility" : "Opens up a narrow mix slightly"));
        }

        if (imagerOn)
        {
            p[pImgOn] = 1.0f;
            reason ({ pImgOn }, "Stereo imager handles width and mono bass");
        }
    }

    if (cancelled && cancelled())
        return {};

    //--------------------------------------------------------------------------
    // Limiter: find the drive that hits the loudness target on the simulated chain.
    p[pLimOn] = 1.0f;
    p[pLimTruePeak] = 1.0f;
    p[pLimCeiling] = ceiling;
    if (! request.loudnessOnly)
        p[pLimRelease] = targetLufs >= -11.0f ? 60.0f : 100.0f;

    std::vector<float> preL (clip.left, clip.left + clip.numSamples), preR (clip.right, clip.right + clip.numSamples);
    {
        dsp::MasteringChain chain;
        chain.prepare (clip.sampleRate);
        chain.reset (dsp::makeChainSettings (p));
        for (int i = 0; i < clip.numSamples; i += simBlock)
            chain.processPreLimiter (preL.data() + i, preR.data() + i, std::min (simBlock, clip.numSamples - i));
    }

    dsp::LimiterSettings ls;
    ls.enabled = true;
    ls.ceilingDb = ceiling;
    ls.releaseMs = p[pLimRelease];
    ls.truePeak = true;

    auto measure = [&] (float gainDb)
    {
        ls.gainDb = gainDb;
        return runLimiter (preL, preR, clip.sampleRate, ls, stats);
    };

    // The safety limit is on how much the limiter actually limits (average gain
    // reduction on the loud parts), not on the drive knob: driving a quiet,
    // peaky mix by 20 dB can involve very little limiting.
    constexpr float maxDrive = 24.0f;
    constexpr float maxAvgLimiting = 6.0f;
    float drive = 0.0f;
    const auto atZero = measure (0.0f);
    LimiterResult final = atZero;

    auto bisect = [&] (auto tooLow, float lo, float hi)
    {
        for (int it = 0; it < 11; ++it)
        {
            if (cancelled && cancelled())
                break;
            const float mid = 0.5f * (lo + hi);
            if (tooLow (measure (mid))) lo = mid; else hi = mid;
        }
        return std::round (0.5f * (lo + hi) * 10.0f) / 10.0f;
    };

    if (atZero.integrated >= targetLufs - 0.05f)
    {
        if (atZero.integrated > targetLufs + 0.3f)
        {
            // Already louder than the target: trim after the limiter (peaks only get lower).
            p[pOutGain] = std::round ((targetLufs - atZero.integrated) * 10.0f) / 10.0f;
            reason ({ pOutGain }, fmt ("Your track is already %.1f LU louder than the %.1f LUFS target, so the output is trimmed down to match",
                                       atZero.integrated - targetLufs, targetLufs));
        }
        else
        {
            proposal.notes.push_back (fmt ("Your track already reaches %.1f LUFS without extra drive; the limiter only guards the %.1f dBTP ceiling.",
                                           atZero.integrated, ceiling));
        }
    }
    else
    {
        const bool reachable = measure (maxDrive).integrated >= targetLufs;
        drive = reachable ? bisect ([&] (const LimiterResult& r) { return r.integrated < targetLufs; }, 0.0f, maxDrive) : maxDrive;
        final = measure (drive);

        if (final.avgGr > maxAvgLimiting)
        {
            drive = bisect ([&] (const LimiterResult& r) { return r.avgGr < maxAvgLimiting; }, 0.0f, drive);
            final = measure (drive);
            proposal.notes.push_back (fmt ("Reaching %.1f LUFS would need more than %.0f dB of average limiting, which would audibly flatten the track. "
                                           "I stopped at %.1f LUFS - add compression/saturation first or choose a lower target.",
                                           targetLufs, maxAvgLimiting, final.integrated));
        }
    }

    if (cancelled && cancelled())
        return {};

    p[pLimGain] = drive;
    reason ({ pLimOn, pLimGain }, fmt ("Drive found by simulating the whole chain on your audio: %.1f LUFS integrated%s",
                                       final.integrated, request.platform.empty() ? "" : fmt (" (%s target)", request.platform.c_str()).c_str()));
    reason ({ pLimCeiling, pLimTruePeak }, fmt ("True-peak ceiling %.1f dBTP leaves headroom for MP3/AAC encoding", ceiling));
    reason ({ pLimRelease }, targetLufs >= -11.0f ? "Faster release keeps a loud master from sounding pumped" : "Natural release for moderate loudness");

    if (final.avgGr > 5.0f && final.integrated >= targetLufs - 0.2f)
        proposal.notes.push_back (fmt ("Heavy limiting (about %.1f dB on loud parts). Expect some loss of punch; a lower loudness target would preserve more dynamics.",
                                       final.avgGr));

    //--------------------------------------------------------------------------
    proposal.setFromDiff (current, p, reasons);

    // Final verification pass with exactly the proposed settings.
    proposal.prediction = simulate (p, clip);

    if (useRef)
        proposal.summary = fmt ("Matches the reference's tonal balance, loudness (%.1f LUFS) and width as closely as is safe.", targetLufs);
    else if (request.loudnessOnly)
        proposal.summary = fmt ("Sets the limiter so your track reaches %.1f LUFS with a %.1f dBTP ceiling.", targetLufs, ceiling);
    else
        proposal.summary = fmt ("Balanced %s tonal correction, %s, and a true-peak limiter for %.0f LUFS.",
                                tiltWord (input.tiltDbPerOctave),
                                isOn (p, pCompOn) ? "gentle glue compression" : "no extra compression",
                                targetLufs);

    return proposal;
}

//==============================================================================
float findBuildUp (const AnalysisResult& track, float loHz, float hiHz, float minDb, float& amountDb)
{
    amountDb = 0.0f;
    if (! track.valid)
        return 0.0f;

    const auto residual = residualFromLine (smoothCurve (track.normalisedMid(), 2), track.tiltDbPerOctave);
    float bestF = 0.0f;
    for (int k = 0; k < numPoints; ++k)
    {
        const float f = pointFreq (k);
        if (f >= loHz && f <= hiHz && residual[(size_t) k] > amountDb)
        {
            amountDb = residual[(size_t) k];
            bestF = f;
        }
    }

    return amountDb >= minDb ? bestF : 0.0f;
}

//==============================================================================
std::vector<std::string> referenceRecommendations (const AnalysisResult& track, const AnalysisResult& ref)
{
    std::vector<std::string> out;

    const float loud = track.integratedLufs - ref.integratedLufs;
    if (std::abs (loud) >= 1.0f)
        out.push_back (fmt ("Loudness: %.1f LU %s than the reference - %s the limiter drive.", std::abs (loud),
                            loud < 0 ? "quieter" : "louder", loud < 0 ? "raise" : "lower"));

    const float plr = track.plrDb - ref.plrDb;
    if (plr > 3.0f)
        out.push_back (fmt ("Dynamics: %.1f dB more peak-to-loudness range than the reference - gentle compression/limiting would match its density.", plr));
    else if (plr < -3.0f)
        out.push_back (fmt ("Dynamics: %.1f dB denser than the reference - back off compression or limiting.", -plr));

    for (int b = 0; b < AnalysisResult::numBands; ++b)
    {
        const float d = track.bandDb[(size_t) b] - ref.bandDb[(size_t) b];
        if (std::abs (d) >= 2.0f)
            out.push_back (fmt ("%s: %+.1f dB vs reference - %s %s.", AnalysisResult::bandName (b), d,
                                d > 0 ? "cut" : "boost", b <= 1 ? "with the low shelf" : (b == 5 ? "with the high shelf" : "with a broad bell")));
    }

    const float width = track.sideToMidDb - ref.sideToMidDb;
    if (std::abs (width) >= 2.0f)
        out.push_back (fmt ("Width: %s than the reference (side/mid %+.1f dB) - %s the imager width.", width < 0 ? "narrower" : "wider",
                            width, width < 0 ? "raise" : "lower"));

    if (track.lowSideToMidDb > -12.0f && ref.lowSideToMidDb < track.lowSideToMidDb - 3.0f)
        out.push_back ("Low end: wider than the reference below 150 Hz - enable Mono Bass.");

    if (track.truePeakDb > -0.5f)
        out.push_back (fmt ("Peaks: %.1f dBTP true peak - use the limiter's true-peak ceiling to avoid encoder clipping.", track.truePeakDb));

    if (out.empty())
        out.push_back ("Very close to the reference already - only fine-tuning needed.");

    return out;
}

std::string describeAnalysis (const AnalysisResult& t, const AnalysisResult* ref, float targetLufs, bool processed)
{
    std::string s;
    s += processed ? "**Your processed output**" : "**Your track (unprocessed input)**";
    s += fmt (" - %.0f s analysed\n", t.secondsAnalysed);

    // Loudness
    s += fmt ("- **Loudness:** %.1f LUFS integrated, true peak %.1f dBTP", t.integratedLufs, t.truePeakDb);
    const float toTarget = t.integratedLufs - targetLufs;
    if (std::abs (toTarget) < 1.0f)
        s += fmt (" - right on your %.0f LUFS target.\n", targetLufs);
    else
        s += fmt (" - %.1f LU %s than a %.0f LUFS target.\n", std::abs (toTarget), toTarget < 0 ? "quieter" : "louder", targetLufs);

    // Dynamics
    s += fmt ("- **Dynamics:** PLR %.1f dB, loudness range %.1f LU - ", t.plrDb, t.loudnessRange);
    if (t.plrDb > 14.0f)       s += "very dynamic/peaky; compression and limiting will help it compete.\n";
    else if (t.plrDb >= 9.0f)  s += "healthy for a modern master.\n";
    else                       s += "already dense; avoid adding more compression.\n";

    // Tone
    s += fmt ("- **Tone:** %s overall (%.1f dB/oct vs ~%.0f typical)", tiltWord (t.tiltDbPerOctave), t.tiltDbPerOctave, typicalTiltDbPerOctave);
    {
        const auto residual = residualFromLine (smoothCurve (t.normalisedMid(), 3), t.tiltDbPerOctave);
        float worst = 0.0f, worstF = 0.0f;
        for (int k = 0; k < numPoints; ++k)
        {
            const float f = pointFreq (k);
            if (f >= 120.0f && f <= 8000.0f && std::abs (residual[(size_t) k]) > std::abs (worst))
            {
                worst = residual[(size_t) k];
                worstF = f;
            }
        }

        if (std::abs (worst) >= 2.0f)
        {
            const char* name = worstF < 500.0f ? (worst > 0 ? " (often heard as mud)" : " (can sound thin)")
                             : worstF < 1500.0f ? (worst > 0 ? " (can sound boxy/nasal)" : "")
                             : (worst > 0 ? " (can sound harsh)" : " (can sound distant)");
            s += fmt ("; a %s of %.1f dB around %.0f Hz%s", worst > 0 ? "build-up" : "dip", std::abs (worst), worstF, name);
        }
        s += ".\n";
    }

    // Stereo
    s += fmt ("- **Stereo:** correlation %.2f, side/mid %.1f dB", t.correlation, t.sideToMidDb);
    if (t.correlation < 0.0f)        s += " - **phase problem**: parts of the mix cancel in mono.";
    else if (t.correlation < 0.3f)   s += " - very wide; check mono compatibility.";
    else if (t.sideToMidDb < -20.0f) s += " - quite narrow.";
    else                             s += " - healthy.";
    if (t.lowSideToMidDb > -12.0f)
        s += fmt (" Bass has stereo content (%.1f dB) - mono bass below ~120 Hz would tighten it.", t.lowSideToMidDb);
    s += "\n";

    if (t.truePeakDb > -1.0f)
        s += "- **Peaks:** above -1 dBTP; inter-sample peaks may distort after MP3/AAC encoding.\n";

    if (ref != nullptr && ref->valid)
    {
        s += "\n**Compared with your reference:**\n";
        for (auto& line : referenceRecommendations (t, *ref))
            s += "- " + line + "\n";
        s += "\nSay **match the reference** and I'll build a chain that closes these gaps - you approve every change.";
    }
    else
    {
        s += "\nSay **master it** for a full chain proposal, or load a reference track to compare against.";
    }

    return s;
}

} // namespace prizm::assistant
