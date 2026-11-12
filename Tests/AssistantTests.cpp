/*
  ==============================================================================

    PRIZM V2 - offline tests for the intent parser, chat assistant and
    mastering assistant.

  ==============================================================================
*/

#include "TestHarness.h"

#include "Assistant/ChatAssistant.h"

#include <random>

using namespace prizm;
using namespace prizm::assistant;

namespace
{
    const ParamChange* findChange (const Proposal& p, int param)
    {
        for (auto& c : p.changes)
            if (c.param == param)
                return &c;
        return nullptr;
    }

    /** A synthetic "mix": pink-ish noise with a slow level envelope (dynamics),
        optional darkening low-pass and some stereo spread. */
    void makeMix (double fs, double seconds, float level, float lowpassHz, float stereo,
                  std::vector<float>& l, std::vector<float>& r, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<float> n (0.0f, 1.0f);
        const size_t len = (size_t) (fs * seconds);
        l.assign (len, 0.0f);
        r.assign (len, 0.0f);

        double b[7] {};
        double lpL = 0.0, lpR = 0.0;
        const double a = std::exp (-2.0 * dsp::pi * lowpassHz / fs);

        for (size_t i = 0; i < len; ++i)
        {
            const double w = n (rng);
            b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759;
            b[2] = 0.96900 * b[2] + w * 0.1538520; b[3] = 0.86650 * b[3] + w * 0.3104856;
            b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
            const double pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
            b[6] = w * 0.115926;

            const double side = n (rng) * 0.05 * stereo;
            const double env = 0.55 + 0.45 * std::sin (2.0 * dsp::pi * 0.25 * (double) i / fs);   // 4 s swells -> loudness range
            double xl = (pink + side) * env * level, xr = (pink - side) * env * level;

            lpL = (1.0 - a) * xl + a * lpL;
            lpR = (1.0 - a) * xr + a * lpR;
            l[i] = (float) lpL;
            r[i] = (float) lpR;
        }
    }

    dsp::AnalysisResult analyse (double fs, const std::vector<float>& l, const std::vector<float>& r)
    {
        dsp::AudioAnalyzer a;
        a.prepare (fs);
        a.process (l.data(), r.data(), (int) l.size());
        return a.finish();
    }
}

//==============================================================================
TEST_CASE ("Parser: descriptors, intensity and direction")
{
    auto m = parseMessage ("make it a little warmer but less harsh");
    CHECK (m.has (IntentType::Warmth));
    CHECK (m.has (IntentType::Harsh));
    CHECK_NEAR (m.find (IntentType::Warmth)->intensity, 0.5, 1e-6);
    CHECK_NEAR (m.find (IntentType::Warmth)->direction, 1.0, 1e-6);

    CHECK_NEAR (parseMessage ("it's way too bright").find (IntentType::Brightness)->direction, -1.0, 1e-6);
    CHECK_NEAR (parseMessage ("it sounds too dark").find (IntentType::Brightness)->direction, 1.0, 1e-6);
    CHECK_NEAR (parseMessage ("make it darker").find (IntentType::Brightness)->direction, -1.0, 1e-6);
    CHECK_NEAR (parseMessage ("too much bass").find (IntentType::Bass)->direction, -1.0, 1e-6);
    CHECK_NEAR (parseMessage ("not enough low end").find (IntentType::Bass)->direction, 1.0, 1e-6);
    CHECK_NEAR (parseMessage ("much punchier please").find (IntentType::Punch)->intensity, 1.5, 1e-6);

    // V1 matched "hi" inside "this" - must not happen any more.
    CHECK (! parseMessage ("this needs more air").has (IntentType::Greeting));
    CHECK (parseMessage ("this needs more air").has (IntentType::Air));
}

TEST_CASE ("Parser: numbers, platforms, modules and session commands")
{
    auto eq = parseMessage ("boost 3k by 2 dB");
    CHECK (eq.has (IntentType::EqBand));
    CHECK_NEAR (*eq.find (IntentType::EqBand)->freqHz, 3000.0, 1e-3);
    CHECK_NEAR (*eq.find (IntentType::EqBand)->gainDb, 2.0, 1e-3);

    auto cut = parseMessage ("cut 250 Hz by 3db");
    CHECK_NEAR (*cut.find (IntentType::EqBand)->gainDb, -3.0, 1e-3);

    auto lufs = parseMessage ("make it -12 LUFS");
    CHECK (lufs.has (IntentType::LoudnessTarget));
    CHECK_NEAR (*lufs.find (IntentType::LoudnessTarget)->lufs, -12.0, 1e-3);

    auto spotify = parseMessage ("master it for Spotify");
    CHECK (spotify.has (IntentType::Master));
    CHECK_NEAR (*spotify.find (IntentType::LoudnessTarget)->lufs, -14.0, 1e-3);

    CHECK (parseMessage ("turn off the limiter").find (IntentType::ModuleOff)->module == Module::Limiter);
    CHECK (parseMessage ("ratio 3:1").has (IntentType::CompSetting));
    CHECK_NEAR (*parseMessage ("compressor ratio 3:1").find (IntentType::CompSetting)->plainNumber, 3.0, 1e-3);

    CHECK (parseMessage ("yes").has (IntentType::Apply));
    CHECK (parseMessage ("ok make it warmer").has (IntentType::Warmth));
    CHECK (! parseMessage ("ok make it warmer").has (IntentType::Apply));
    CHECK (! parseMessage ("no more bass").has (IntentType::Dismiss));
    CHECK (parseMessage ("undo that").has (IntentType::Undo));
    CHECK (! parseMessage ("the mix sounds flat").has (IntentType::Reset));
    CHECK (parseMessage ("load a reference track").has (IntentType::LoadReference));
    CHECK (! parseMessage ("open my reference track").has (IntentType::Air));
    CHECK (parseMessage ("match the reference").has (IntentType::MatchReference));
    CHECK (parseMessage ("compare with the reference").has (IntentType::Analyze));
    CHECK (parseMessage ("let me compare").has (IntentType::Compare));
    CHECK (parseMessage ("mono the bass below 100 hz").has (IntentType::MonoBass));
}

TEST_CASE ("Chat: tweak proposals are real parameter changes awaiting approval")
{
    AssistantContext ctx;
    ctx.params = defaultParamState();

    auto r = respond ("make it a little warmer", ctx);
    CHECK (r.proposal.has_value());
    CHECK (r.action == ActionType::None);
    auto* ls = findChange (*r.proposal, pEqLsGain);
    CHECK (ls != nullptr);
    if (ls != nullptr)
        CHECK_NEAR (ls->to, 1.0, 1e-3);

    auto eq = respond ("boost 3k by 2 dB", ctx);
    CHECK (eq.proposal.has_value());
    CHECK (findChange (*eq.proposal, pEqP3Freq) != nullptr && findChange (*eq.proposal, pEqP3Freq)->to == 3000.0f);
    CHECK (findChange (*eq.proposal, pEqP3Gain) != nullptr && findChange (*eq.proposal, pEqP3Gain)->to == 2.0f);

    // Safety: huge boosts are capped at +6 dB.
    ctx.params[pEqHsGain] = 5.0f;
    auto bright = respond ("make it much brighter", ctx);
    CHECK (findChange (*bright.proposal, pEqHsGain) != nullptr && findChange (*bright.proposal, pEqHsGain)->to <= 6.0f);
    ctx.params[pEqHsGain] = 0.0f;

    // Legacy V1 commands now do something real.
    auto tel = respond ("telephone effect", ctx);
    CHECK (tel.proposal && findChange (*tel.proposal, pEqHpfOn) && findChange (*tel.proposal, pEqLpfOn));
    auto punch = respond ("add punch", ctx);
    CHECK (punch.proposal && findChange (*punch.proposal, pCompOn));

    ctx.params[pLimOn] = 1.0f;
    auto off = respond ("turn off the limiter", ctx);
    CHECK (off.proposal && findChange (*off.proposal, pLimOn) && findChange (*off.proposal, pLimOn)->to == 0.0f);

    CHECK (respond ("yes", ctx).action == ActionType::ApplyPending);
    CHECK (respond ("master it for youtube", ctx).action == ActionType::RunMaster);
    CHECK_NEAR (respond ("master it for youtube", ctx).master.targetLufs, -14.0, 1e-3);
    CHECK (respond ("reset", ctx).proposal.has_value());
}

TEST_CASE ("Mastering assistant: hits the loudness target and respects the ceiling")
{
    const double fs = 44100.0;
    std::vector<float> l, r;
    makeMix (fs, 12.0, 0.08f, 600.0f, 1.0f, l, r, 11);    // very quiet, dark, dynamic mix

    const auto input = analyse (fs, l, r);
    CHECK (input.valid);
    REPORT ("synthetic mix: " << input.integratedLufs << " LUFS, tilt " << input.tiltDbPerOctave << " dB/oct, PLR "
            << input.plrDb << " dB, LRA " << input.loudnessRange << " LU");

    MasterRequest req;
    req.targetLufs = -14.0f;
    req.ceilingDb = -1.0f;
    AudioClip clip { l.data(), r.data(), (int) l.size(), fs };

    const auto p = buildMasteringProposal (req, defaultParamState(), input, nullptr, clip);
    CHECK (! p.empty());
    CHECK (p.prediction.valid);
    REPORT ("proposal '" << p.title << "': predicted " << p.prediction.integratedLufs << " LUFS, TP "
            << p.prediction.truePeakDb << " dBTP, limiter avg GR " << p.prediction.avgLimiterGrDb << " dB, "
            << p.changes.size() << " changes");
    CHECK_NEAR (p.prediction.integratedLufs, -14.0, 0.3);
    CHECK (p.prediction.truePeakDb <= -1.0f + 0.2f);

    // Dark input -> the assistant brightens (positive high shelf); quiet input -> clean gain staging.
    CHECK (input.tiltDbPerOctave < -6.5f);
    auto* hs = findChange (p, pEqHsGain);
    CHECK (hs != nullptr && hs->to > 0.0f);
    CHECK (findChange (p, pInGain) != nullptr && findChange (p, pInGain)->to > 6.0f);
    CHECK (p.prediction.avgLimiterGrDb <= 6.5f);

    // Applying the proposal and simulating again gives the same result (the prediction is honest).
    const auto applied = p.applyTo (defaultParamState());
    const auto check = simulate (applied, clip);
    CHECK_NEAR (check.integratedLufs, p.prediction.integratedLufs, 0.05);

    // Every change has a reason.
    for (auto& c : p.changes)
        CHECK (! c.reason.empty());
}

TEST_CASE ("Mastering assistant: reference matching moves towards the reference")
{
    const double fs = 44100.0;
    std::vector<float> l, r, rl, rr;
    makeMix (fs, 12.0, 0.08f, 2500.0f, 1.0f, l, r, 21);     // dark, quiet
    makeMix (fs, 12.0, 0.25f, 16000.0f, 2.0f, rl, rr, 22);  // bright, louder, wider reference

    const auto input = analyse (fs, l, r);
    const auto reference = analyse (fs, rl, rr);

    MasterRequest req;
    req.matchReference = true;
    AudioClip clip { l.data(), r.data(), (int) l.size(), fs };
    const auto p = buildMasteringProposal (req, defaultParamState(), input, &reference, clip);

    REPORT ("reference " << reference.integratedLufs << " LUFS; match predicted " << p.prediction.integratedLufs << " LUFS");
    CHECK_NEAR (p.prediction.integratedLufs, reference.integratedLufs, 0.4);
    auto* hs = findChange (p, pEqHsGain);
    CHECK (hs != nullptr && hs->to > 1.0f);

    // Analysing the processed result: closer to the reference tilt than before.
    dsp::MasteringChain chain;
    chain.prepare (fs);
    chain.reset (dsp::makeChainSettings (p.applyTo (defaultParamState())));
    auto pl = l, pr = r;
    for (size_t i = 0; i < pl.size(); i += 512)
        chain.process (pl.data() + i, pr.data() + i, (int) std::min<size_t> (512, pl.size() - i));
    const auto after = analyse (fs, pl, pr);
    REPORT ("tilt: input " << input.tiltDbPerOctave << ", processed " << after.tiltDbPerOctave << ", reference " << reference.tiltDbPerOctave);
    CHECK (std::abs (after.tiltDbPerOctave - reference.tiltDbPerOctave) < std::abs (input.tiltDbPerOctave - reference.tiltDbPerOctave));

    const auto recs = referenceRecommendations (input, reference);
    CHECK (! recs.empty());
    const auto report = describeAnalysis (input, &reference, -14.0f, false);
    CHECK (report.find ("Loudness") != std::string::npos);
}
