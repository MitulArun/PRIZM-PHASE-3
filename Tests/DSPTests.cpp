/*
  ==============================================================================

    PRIZM V2 - offline tests for the DSP engine and assistant.

    Build & run (no JUCE needed): Tests/run_tests.sh

  ==============================================================================
*/

#include "TestHarness.h"

#include "DSP/AudioAnalyzer.h"
#include "DSP/LoudnessMeter.h"
#include "DSP/MasteringChain.h"
#include "DSP/Oversampler.h"
#include "DSP/TruePeak.h"

#include <random>

using namespace prizm;
using namespace prizm::dsp;

namespace
{
    std::vector<float> sine (double fs, double freq, double seconds, double amplitude, double phase = 0.0)
    {
        std::vector<float> v ((size_t) (fs * seconds));
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (amplitude * std::sin (2.0 * pi * freq * (double) i / fs + phase));
        return v;
    }

    void append (std::vector<float>& dst, const std::vector<float>& src) { dst.insert (dst.end(), src.begin(), src.end()); }

    float measureIntegrated (double fs, const std::vector<float>& l, const std::vector<float>& r, LoudnessMeter* outMeter = nullptr)
    {
        LoudnessMeter m;
        m.prepare (fs);
        for (size_t i = 0; i < l.size(); i += 512)
        {
            const int n = (int) std::min<size_t> (512, l.size() - i);
            m.process (l.data() + i, r.data() + i, n);
        }
        if (outMeter != nullptr)
            *outMeter = m;
        return m.getIntegrated();
    }

    double rmsDb (const float* x, size_t n)
    {
        double s = 0.0;
        for (size_t i = 0; i < n; ++i)
            s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) n + 1e-30);
    }

    int indexOfMaxAbs (const std::vector<float>& v)
    {
        int best = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (std::abs (v[i]) > std::abs (v[(size_t) best]))
                best = (int) i;
        return best;
    }

    void runChain (MasteringChain& chain, std::vector<float>& l, std::vector<float>& r, int block = 480)
    {
        for (size_t i = 0; i < l.size(); i += (size_t) block)
        {
            const int n = (int) std::min<size_t> ((size_t) block, l.size() - i);
            chain.process (l.data() + i, r.data() + i, n);
        }
    }

    float maxSecondDifference (const std::vector<float>& v, size_t from, size_t to)
    {
        float worst = 0.0f;
        for (size_t i = std::max<size_t> (from, 2); i < std::min (to, v.size()); ++i)
            worst = std::max (worst, std::abs (v[i] - 2.0f * v[i - 1] + v[i - 2]));
        return worst;
    }
}

//==============================================================================
TEST_CASE ("Loudness: EBU Tech 3341 case 1 (1 kHz sine, -23 dBFS stereo) at several sample rates")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto s = sine (fs, 1000.0, 20.0, dbToGain (-23.0));
        LoudnessMeter m;
        const float integrated = measureIntegrated (fs, s, s, &m);
        CHECK_NEAR (integrated, -23.0, 0.1);
        CHECK_NEAR (m.getMomentary(), -23.0, 0.1);
        CHECK_NEAR (m.getShortTerm(), -23.0, 0.1);
    }
}

TEST_CASE ("Loudness: EBU Tech 3341 cases 3 and 4 (relative and absolute gating)")
{
    const double fs = 48000.0;
    std::vector<float> s;
    append (s, sine (fs, 1000.0, 10.0, dbToGain (-36.0)));
    append (s, sine (fs, 1000.0, 60.0, dbToGain (-23.0)));
    append (s, sine (fs, 1000.0, 10.0, dbToGain (-36.0)));
    CHECK_NEAR (measureIntegrated (fs, s, s), -23.0, 0.1);

    std::vector<float> t;
    append (t, sine (fs, 1000.0, 10.0, dbToGain (-72.0)));
    append (t, sine (fs, 1000.0, 10.0, dbToGain (-36.0)));
    append (t, sine (fs, 1000.0, 60.0, dbToGain (-23.0)));
    append (t, sine (fs, 1000.0, 10.0, dbToGain (-36.0)));
    append (t, sine (fs, 1000.0, 10.0, dbToGain (-72.0)));
    CHECK_NEAR (measureIntegrated (fs, t, t), -23.0, 0.1);
}

TEST_CASE ("Loudness: EBU Tech 3342 case 1 (loudness range 10 LU)")
{
    const double fs = 48000.0;
    std::vector<float> s;
    append (s, sine (fs, 1000.0, 20.0, dbToGain (-20.0)));
    append (s, sine (fs, 1000.0, 20.0, dbToGain (-30.0)));
    LoudnessMeter m;
    measureIntegrated (fs, s, s, &m);
    CHECK_NEAR (m.getLoudnessRange(), 10.0, 1.0);
}

TEST_CASE ("True peak: inter-sample peak of a fs/4 sine at 45 degrees")
{
    const double fs = 48000.0;
    const auto s = sine (fs, fs / 4.0, 1.0, 1.0, pi / 4.0);
    TruePeakDetector tp;
    float samplePeak = 0.0f, truePeak = 0.0f;
    for (float x : s)
    {
        samplePeak = std::max (samplePeak, std::abs (x));
        truePeak = std::max (truePeak, tp.process (x));
    }
    CHECK_NEAR (gainToDb (samplePeak), -3.01, 0.05);
    CHECK_NEAR (gainToDb (truePeak), 0.0, 0.5);
}

TEST_CASE ("Oversampler: integer latency and flat passband")
{
    Oversampler4x os;
    os.prepare (512);
    const int latency = os.getLatencySamples();
    CHECK (latency > 0);

    for (double freq : { 100.0, 1000.0, 10000.0, 18000.0 })
    {
        const double fs = 44100.0;
        auto in = sine (fs, freq, 0.5, 0.5);
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); i += 512)
        {
            const int n = (int) std::min<size_t> (512, in.size() - i);
            float* up = os.upsample (0, in.data() + i, n);
            os.downsample (0, up, out.data() + i, n);
        }

        // Compare against the input delayed by exactly 'latency' samples.
        double err = 0.0, ref = 0.0;
        for (size_t i = 2000; i < in.size(); ++i)
        {
            const double d = out[i] - in[i - (size_t) latency];
            err += d * d;
            ref += (double) in[i - (size_t) latency] * in[i - (size_t) latency];
        }
        const double nullDb = 10.0 * std::log10 (err / ref + 1e-30);
        INFO ("freq " << freq << " Hz null " << nullDb << " dB");
        REPORT ("oversampler round trip at " << freq << " Hz: null vs delayed input " << nullDb << " dB (latency " << latency << ")");
        CHECK (nullDb < (freq < 15000.0 ? -60.0 : -30.0));
    }
}

TEST_CASE ("EQ: flat settings are transparent, a +6 dB bell measures +6 dB")
{
    const double fs = 48000.0;
    ParametricEQ eq;
    eq.prepare (fs);
    EQSettings flat;
    eq.reset (flat);

    auto l = sine (fs, 997.0, 1.0, 0.5), r = l;
    const auto original = l;
    eq.process (l.data(), r.data(), (int) l.size());
    float maxErr = 0.0f;
    for (size_t i = 0; i < l.size(); ++i)
        maxErr = std::max (maxErr, std::abs (l[i] - original[i]));
    CHECK (maxErr < 1.0e-5f);

    EQSettings bell;
    bell.pFreq[1] = 1000.0f;
    bell.pGain[1] = 6.0f;
    bell.pQ[1] = 1.0f;
    eq.reset (bell);
    l = sine (fs, 1000.0, 1.0, 0.25);
    r = l;
    const double before = rmsDb (l.data() + 24000, 24000);
    eq.process (l.data(), r.data(), (int) l.size());
    CHECK_NEAR (rmsDb (l.data() + 24000, 24000) - before, 6.0, 0.1);
    CHECK_NEAR (ParametricEQ::responseDb (bell, fs, 1000.0), 6.0, 0.01);
}

TEST_CASE ("Compressor: static gain reduction matches the gain computer")
{
    const double fs = 48000.0;
    Compressor comp;
    comp.prepare (fs);
    CompressorSettings s;
    s.enabled = true;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.attackMs = 1.0f;
    s.releaseMs = 50.0f;
    comp.reset (s);

    auto l = sine (fs, 1000.0, 2.0, dbToGain (-10.0)), r = l;
    const double before = rmsDb (l.data() + 48000, 48000);
    comp.process (l.data(), r.data(), (int) l.size());
    const double after = rmsDb (l.data() + 48000, 48000);

    // Detector is RMS-style: a -10 dBFS-peak sine reads -13.01 dB.
    const double expectedGr = (-13.01 + 20.0) * (1.0 - 1.0 / 4.0);
    CHECK_NEAR (before - after, expectedGr, 0.3);
    CHECK_NEAR (comp.getGainReductionDb(), expectedGr, 0.3);
}

TEST_CASE ("Limiter: output never exceeds the ceiling, latency is exact")
{
    const double fs = 44100.0;
    Limiter lim;
    lim.prepare (fs);
    LimiterSettings s;
    s.enabled = true;
    s.gainDb = 12.0f;
    s.ceilingDb = -1.0f;
    s.releaseMs = 80.0f;
    s.truePeak = true;
    lim.reset (s);

    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 0.3f);
    std::vector<float> l (44100 * 5), r (l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        const float env = (i / 4410) % 2 == 0 ? 1.0f : 0.25f;   // bursts to exercise attack/release
        l[i] = noise (rng) * env;
        r[i] = noise (rng) * env;
    }

    float maxGr = 0.0f;
    for (size_t i = 0; i < l.size(); i += 256)
    {
        lim.process (l.data() + i, r.data() + i, (int) std::min<size_t> (256, l.size() - i));
        maxGr = std::max (maxGr, lim.getGainReductionDb());
    }

    float samplePeak = 0.0f, truePeak = 0.0f;
    TruePeakDetector tpL, tpR;
    for (size_t i = 0; i < l.size(); ++i)
    {
        samplePeak = std::max ({ samplePeak, std::abs (l[i]), std::abs (r[i]) });
        truePeak = std::max ({ truePeak, tpL.process (l[i]), tpR.process (r[i]) });
    }

    INFO ("sample peak " << gainToDb (samplePeak) << " dBFS, true peak " << gainToDb (truePeak) << " dBTP");
    REPORT ("limiter +12 dB drive: sample peak " << gainToDb (samplePeak) << " dBFS, true peak "
            << gainToDb (truePeak) << " dBTP, max GR " << maxGr << " dB");
    CHECK (gainToDb (samplePeak) <= -1.0f + 0.001f);
    CHECK (gainToDb (truePeak) <= -1.0f + 0.3f);
    CHECK (maxGr > 3.0f);

    // Latency: a quiet impulse passes untouched and arrives exactly 'latency' samples later.
    Limiter quiet;
    quiet.prepare (fs);
    LimiterSettings q;
    q.enabled = true;
    quiet.reset (q);
    std::vector<float> il (4096, 0.0f), ir (4096, 0.0f);
    il[100] = ir[100] = 0.1f;
    quiet.process (il.data(), ir.data(), 4096);
    CHECK_EQ (indexOfMaxAbs (il), 100 + quiet.getLatencySamples());
    CHECK_NEAR (il[(size_t) (100 + quiet.getLatencySamples())], 0.1, 1.0e-6);
}

TEST_CASE ("Imager: width 0 is mono, mono bass removes low side content")
{
    const double fs = 48000.0;
    StereoImager img;
    img.prepare (fs);

    ImagerSettings s;
    s.enabled = true;
    s.widthPercent = 0.0f;
    img.reset (s);
    auto l = sine (fs, 440.0, 0.5, 0.5), r = sine (fs, 660.0, 0.5, 0.3);
    img.process (l.data(), r.data(), (int) l.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < l.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (l[i] - r[i]));
    CHECK (maxDiff < 1.0e-6f);

    // Pure side signal (L = -R) at 40 Hz vs 4 kHz with mono bass at 120 Hz.
    s.widthPercent = 100.0f;
    s.monoBassOn = true;
    s.monoBassHz = 120.0f;
    for (double freq : { 40.0, 4000.0 })
    {
        img.reset (s);
        auto sl = sine (fs, freq, 1.0, 0.5);
        std::vector<float> sr (sl.size());
        for (size_t i = 0; i < sl.size(); ++i)
            sr[i] = -sl[i];
        const double before = rmsDb (sl.data() + 24000, 24000);
        img.process (sl.data(), sr.data(), (int) sl.size());
        const double change = rmsDb (sl.data() + 24000, 24000) - before;
        INFO ("side tone " << freq << " Hz changed by " << change << " dB");
        if (freq < 100.0)
            CHECK (change < -20.0);
        else
            CHECK (std::abs (change) < 0.1);
    }
}

TEST_CASE ("Chain: constant latency whatever is enabled, and the A/B original path is aligned")
{
    const double fs = 48000.0;
    ParamState p = defaultParamState();

    auto impulseResponseIndex = [&] (const ParamState& params)
    {
        MasteringChain chain;
        chain.prepare (fs);
        chain.reset (makeChainSettings (params));
        std::vector<float> l (8192, 0.0f), r (8192, 0.0f);
        l[50] = r[50] = 0.05f;
        runChain (chain, l, r);
        return std::pair<int, int> (indexOfMaxAbs (l), chain.getLatencySamples());
    };

    const auto allOff = impulseResponseIndex (p);
    CHECK_EQ (allOff.first, 50 + allOff.second);

    ParamState on = p;
    on[pSatOn] = 1.0f;  on[pSatMix] = 0.0f;   // linear path through the oversampler
    on[pLimOn] = 1.0f;
    on[pImgOn] = 1.0f;
    on[pCompOn] = 1.0f; on[pCompThresh] = 0.0f;
    const auto allOn = impulseResponseIndex (on);
    CHECK_EQ (allOn.second, allOff.second);
    CHECK_EQ (allOn.first, allOff.first);
}

TEST_CASE ("Chain: switching modules, A/B and mono check is click-free")
{
    const double fs = 48000.0;
    ParamState p = defaultParamState();
    MasteringChain chain;
    chain.prepare (fs);
    chain.reset (makeChainSettings (p));

    auto l = sine (fs, 220.0, 4.0, 0.5), r = sine (fs, 220.0, 4.0, 0.5, 0.3);
    const int block = 256;
    ParamState live = p;

    for (size_t i = 0, b = 0; i < l.size(); i += block, ++b)
    {
        // Every ~0.1 s flip something, including big EQ jumps and the A/B monitor.
        if (b % 19 == 5)
        {
            live[pEqP2Gain] = live[pEqP2Gain] > 0.0f ? -9.0f : 9.0f;
            live[pSatOn] = 1.0f - live[pSatOn];
            live[pLimOn] = 1.0f - live[pLimOn];
            live[pLimGain] = live[pLimOn] > 0.5f ? 9.0f : 0.0f;
            live[pImgOn] = 1.0f - live[pImgOn];
            live[pImgWidth] = live[pImgWidth] > 100.0f ? 50.0f : 180.0f;
            live[pEqHpfOn] = 1.0f - live[pEqHpfOn];
            live[pEqHpfFreq] = 150.0f;
            chain.setSettings (makeChainSettings (live));

            MonitorSettings m;
            m.listenToOriginal = (b / 19) % 2 == 1;
            m.monoCheck = (b / 38) % 2 == 1;
            chain.setMonitor (m);
        }

        const int n = (int) std::min<size_t> ((size_t) block, l.size() - i);
        chain.process (l.data() + i, r.data() + i, n);
    }

    // A clean 220 Hz sine at 0.5 has a max second difference of ~0.5*(2*pi*220/48000)^2 = 4e-4.
    // Even heavy limiting stays far below the ~0.05+ a hard switch would produce.
    const float worst = std::max (maxSecondDifference (l, 4800, l.size()), maxSecondDifference (r, 4800, r.size()));
    INFO ("worst second difference " << worst);
    REPORT ("click test: worst second difference while switching " << worst << " (hard switch would be > 0.05)");
    CHECK (worst < 0.02f);
}

TEST_CASE ("Analyzer: correlation, width and spectral tilt")
{
    const double fs = 44100.0;
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.1f);

    // Identical channels (mono): correlation 1, practically no side.
    {
        AudioAnalyzer a;
        a.prepare (fs);
        std::vector<float> x (44100 * 6);
        for (auto& v : x) v = noise (rng);
        a.process (x.data(), x.data(), (int) x.size());
        auto res = a.finish();
        CHECK (res.valid);
        CHECK_NEAR (res.correlation, 1.0, 1.0e-4);
        CHECK (res.sideToMidDb < -50.0f);
        CHECK_NEAR (res.tiltDbPerOctave, 0.0, 0.5);      // white noise is flat
    }

    // Independent channels: correlation ~0, side ~= mid.
    {
        AudioAnalyzer a;
        a.prepare (fs);
        std::vector<float> l (44100 * 6), r (l.size());
        for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
        a.process (l.data(), r.data(), (int) l.size());
        auto res = a.finish();
        CHECK_NEAR (res.correlation, 0.0, 0.03);
        CHECK_NEAR (res.sideToMidDb, 0.0, 0.5);
    }

    // Pink-ish noise (Paul Kellet's filter): about -3 dB/octave.
    {
        AudioAnalyzer a;
        a.prepare (fs);
        std::vector<float> x (44100 * 8);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (auto& v : x)
        {
            const double w = noise (rng);
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            v = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11);
            b6 = w * 0.115926;
        }
        a.process (x.data(), x.data(), (int) x.size());
        auto res = a.finish();
        CHECK_NEAR (res.tiltDbPerOctave, -3.0, 0.6);

        AnalysisResult back;
        CHECK (AnalysisResult::deserialise (res.serialise(), back));
        CHECK_NEAR (back.tiltDbPerOctave, res.tiltDbPerOctave, 1.0e-3);
        CHECK_NEAR (back.midDb[10], res.midDb[10], 1.0e-3);
    }
}
