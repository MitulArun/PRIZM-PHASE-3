/*
  ==============================================================================

    PRIZM V2 - chat assistant.

  ==============================================================================
*/

#include "ChatAssistant.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace prizm::assistant
{

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

    // Limits for assistant-initiated moves (explicit numbers from the user may go further, up to the parameter range).
    constexpr float maxAssistEqDb = 6.0f;
    constexpr float maxAssistRatio = 4.0f;
    constexpr float maxAssistLimiterDrive = 12.0f;
    constexpr float maxAssistWidth = 160.0f;
    constexpr float maxAssistSatDrive = 12.0f;

    struct Builder
    {
        const AssistantContext& ctx;
        ParamState work;
        std::vector<std::string> reasons;
        std::vector<std::string> notes;
        std::vector<std::string> titleParts;

        explicit Builder (const AssistantContext& c) : ctx (c), work (c.params), reasons ((size_t) NumParams) {}

        float get (int p) const { return work[(size_t) p]; }

        void set (int p, float v, const std::string& why)
        {
            work[(size_t) p] = clampParam (p, v);
            reasons[(size_t) p] = why;
        }

        /** Adds to a parameter, clamped to +/-limit, with a safety note when clamped. */
        void add (int p, float delta, float limit, const std::string& why)
        {
            const float wanted = get (p) + delta;
            const float v = dsp::clampValue (wanted, -limit, limit);
            if (std::abs (v - wanted) > 0.05f)
                notes.push_back (fmt ("Safety: %s limited to %s (mastering-safe range).", paramInfo (p).name, formatParamValue (p, v).c_str()));
            set (p, v, why);
        }

        void enable (int p, const std::string& why)
        {
            if (! isOn (work, p))
                set (p, 1.0f, why);
        }

        const dsp::AnalysisResult* analysis() const
        {
            return (ctx.recentInput != nullptr && ctx.recentInput->valid) ? ctx.recentInput : nullptr;
        }

        /** Loud-part detector level (dBFS RMS) at the compressor input, if known. */
        std::optional<float> loudLevel() const
        {
            if (auto* a = analysis())
                return a->blockRmsP90 + get (pInGain);
            if (ctx.live.audioPlaying && ctx.live.inputRmsDb > -60.0f)
                return ctx.live.inputRmsDb + 3.0f + get (pInGain);
            return std::nullopt;
        }

        void setupCompressor (float ratio, float attack, float release, float knee, float grTarget, const std::string& why)
        {
            enable (pCompOn, why);
            set (pCompRatio, std::min (ratio, maxAssistRatio), why);
            set (pCompAttack, attack, why);
            set (pCompRelease, release, why);
            set (pCompKnee, knee, why);
            set (pCompMix, 100.0f, why);
            if (get (pCompScHpf) < 40.0f)
                set (pCompScHpf, 60.0f, "Detector ignores sub-bass so the low end doesn't pump the mix");

            if (auto level = loudLevel())
            {
                const float thr = *level - grTarget / (1.0f - 1.0f / get (pCompRatio));
                set (pCompThresh, std::round (thr * 2.0f) / 2.0f, fmt ("Set so loud parts get ~%.1f dB of gain reduction", grTarget));
                set (pCompMakeup, std::round (grTarget * 0.8f * 2.0f) / 2.0f, "Makeup keeps the level roughly the same for a fair comparison");
            }
            else
            {
                set (pCompThresh, -24.0f, "Starting threshold (play your track and ask again to calibrate it)");
                set (pCompMakeup, 1.5f, "Approximate makeup gain");
                notes.push_back ("I couldn't hear any audio yet, so the threshold is a starting point. Play your track and ask again to calibrate it.");
            }
        }

        float tiltOrTypical() const
        {
            if (auto* a = analysis())
                return a->tiltDbPerOctave;
            return typicalTiltDbPerOctave;
        }

        /** Finds a problem frequency from analysis, falling back to a default. */
        float problemFreq (float lo, float hi, float fallback, std::string& detail) const
        {
            if (auto* a = analysis())
            {
                float amount = 0.0f;
                const float f = findBuildUp (*a, lo, hi, 1.0f, amount);
                if (f > 0.0f)
                {
                    detail = fmt (" (I measured a %.1f dB build-up at %.0f Hz)", amount, f);
                    return std::round (f);
                }
            }
            return fallback;
        }
    };

    std::string describeDirection (float dir, const char* more, const char* less) { return dir >= 0.0f ? more : less; }

    //==========================================================================
    void handleTweak (Builder& b, const Intent& in)
    {
        const float k = in.intensity;
        const float dir = in.direction;
        auto* a = b.analysis();

        switch (in.type)
        {
            case IntentType::Warmth:
            {
                float amount = 2.0f * k * dir;
                if (dir > 0 && a != nullptr && a->tiltDbPerOctave < -6.5f)
                {
                    amount *= 0.5f;
                    b.notes.push_back ("Your mix already leans dark, so I halved the warmth boost.");
                }
                b.enable (pEqOn, "EQ needed for this change");
                if (std::abs (b.get (pEqLsGain)) < 0.05f)
                    b.set (pEqLsFreq, 150.0f, "Low shelf at 150 Hz adds body without boom");
                b.add (pEqLsGain, amount, maxAssistEqDb, dir > 0 ? "Adds warmth and body" : "Thins out the low end");
                b.add (pEqHsGain, -1.0f * k * dir, maxAssistEqDb, dir > 0 ? "Slightly softer top end makes it feel warmer" : "Slightly more top end");
                b.titleParts.push_back (describeDirection (dir, "warmer", "less warm"));
                break;
            }

            case IntentType::Brightness:
            case IntentType::Air:
            {
                const bool air = in.type == IntentType::Air;
                float amount = 2.0f * k * dir;
                if (dir > 0 && b.tiltOrTypical() > -3.5f)
                {
                    amount *= 0.5f;
                    b.notes.push_back ("Your mix is already on the bright side, so I kept the boost small.");
                }
                b.enable (pEqOn, "EQ needed for this change");
                if (std::abs (b.get (pEqHsGain)) < 0.05f || air)
                    b.set (pEqHsFreq, air ? 12000.0f : 6000.0f, air ? "High shelf at 12 kHz adds air without harshness"
                                                                   : "High shelf at 6 kHz for clarity and sparkle");
                b.add (pEqHsGain, amount, maxAssistEqDb, dir > 0 ? (air ? "Adds air" : "Brighter, clearer top end") : "Darker, smoother top end");
                b.titleParts.push_back (air ? describeDirection (dir, "more air", "less air") : describeDirection (dir, "brighter", "darker"));
                break;
            }

            case IntentType::Bass:
            case IntentType::Sub:
            {
                const bool sub = in.type == IntentType::Sub;
                float amount = 2.0f * k * dir;
                if (dir > 0 && a != nullptr && a->bandDb[sub ? 0 : 1] > (sub ? -8.0f : -3.0f))
                {
                    amount *= 0.5f;
                    b.notes.push_back ("The low end is already strong in your mix, so I limited the bass boost.");
                }
                b.enable (pEqOn, "EQ needed for this change");
                b.set (pEqLsFreq, sub ? 60.0f : 100.0f, sub ? "Shelf focused on the sub region" : "Shelf on the bass region");
                b.add (pEqLsGain, amount, maxAssistEqDb, dir > 0 ? "More low end" : "Less low end");
                b.titleParts.push_back (sub ? describeDirection (dir, "more sub", "less sub") : describeDirection (dir, "more bass", "less bass"));
                break;
            }

            case IntentType::Mud:
            case IntentType::Boxy:
            case IntentType::Harsh:
            case IntentType::Sibilance:
            {
                struct Spec { int freqParam; float lo, hi, fallback, q; const char* label; const char* what; };
                const Spec spec = in.type == IntentType::Mud ? Spec { pEqP1Freq, 180.0f, 500.0f, 300.0f, 1.2f, "less muddy", "Cuts low-mid mud" }
                                : in.type == IntentType::Boxy ? Spec { pEqP2Freq, 400.0f, 1000.0f, 600.0f, 1.4f, "less boxy", "Cuts boxy/nasal mids" }
                                : in.type == IntentType::Harsh ? Spec { pEqP3Freq, 2000.0f, 5000.0f, 3500.0f, 1.5f, "less harsh", "Tames harsh upper mids" }
                                : Spec { pEqP3Freq, 5000.0f, 9000.0f, 7000.0f, 2.5f, "less sibilant", "Softens sibilance" };

                std::string detail;
                const float freq = in.freqHz ? *in.freqHz : b.problemFreq (spec.lo, spec.hi, spec.fallback, detail);
                b.enable (pEqOn, "EQ needed for this change");
                b.set (spec.freqParam, freq, std::string (spec.what) + detail);
                b.set (spec.freqParam + 2, spec.q, "Fairly narrow so only the problem area is touched");
                b.add (spec.freqParam + 1, -2.0f * k, maxAssistEqDb, std::string (spec.what) + detail);
                b.titleParts.push_back (spec.label);
                break;
            }

            case IntentType::Presence:
            {
                b.enable (pEqOn, "EQ needed for this change");
                b.set (pEqP3Freq, 3000.0f, "Presence region (vocals, snare, guitars)");
                b.set (pEqP3Q, 1.0f, "Broad bell");
                b.add (pEqP3Gain, 1.5f * k * dir, maxAssistEqDb, dir > 0 ? "Brings vocals and leads forward" : "Pushes vocals back slightly");
                b.titleParts.push_back (describeDirection (dir, "more presence", "less presence"));
                break;
            }

            case IntentType::Punch:
            {
                if (dir < 0)
                {
                    if (isOn (b.work, pCompOn))
                        b.set (pCompAttack, std::max (2.0f, b.get (pCompAttack) * 0.5f), "Faster attack catches transients (less punch)");
                    b.titleParts.push_back ("less punch");
                    break;
                }
                b.setupCompressor (2.5f, 20.0f, 120.0f, 6.0f, 2.5f * k, "Punch: 20 ms attack lets transients through, 2.5:1 adds density");
                b.titleParts.push_back ("punchier");
                break;
            }

            case IntentType::Glue:
            {
                if (dir < 0)
                {
                    if (isOn (b.work, pCompOn))
                    {
                        const float ratio = b.get (pCompRatio) - 0.8f * k;
                        if (ratio <= 1.2f)
                            b.set (pCompOn, 0.0f, "Compression removed as requested");
                        else
                        {
                            b.set (pCompRatio, ratio, "Gentler ratio");
                            b.set (pCompThresh, b.get (pCompThresh) + 3.0f * k, "Higher threshold = less gain reduction");
                        }
                    }
                    else
                    {
                        b.notes.push_back ("The compressor is already off.");
                    }
                    b.titleParts.push_back ("less compression");
                    break;
                }

                if (a != nullptr && a->plrDb < 8.0f)
                    b.notes.push_back (fmt ("Your track is already dense (PLR %.1f dB) - I kept the compression very light.", a->plrDb));

                const float gr = (a != nullptr && a->plrDb < 8.0f) ? 1.0f : 1.5f * k;
                b.setupCompressor (1.8f, 30.0f, 250.0f, 9.0f, gr, "Glue: gentle 1.8:1 with a soft knee and slow release");
                // V1 'smooth' also added a touch of warmth and high roll-off.
                b.enable (pEqOn, "EQ needed for this change");
                b.add (pEqLsGain, 0.5f * k, maxAssistEqDb, "A touch of warmth for a smoother feel");
                b.add (pEqHsGain, -0.5f * k, maxAssistEqDb, "Slight high roll-off for a smoother feel");
                b.titleParts.push_back ("smoother / glued");
                break;
            }

            case IntentType::Tight:
            {
                b.setupCompressor (3.0f, 5.0f, 100.0f, 3.0f, 3.0f * k, "Tight: fast attack and release control peaks");
                b.enable (pEqOn, "EQ needed for this change");
                b.set (pEqP1Freq, 350.0f, "Low-mid area");
                b.add (pEqP1Gain, -1.5f * k, maxAssistEqDb, "Clears low-mid clutter");
                b.set (pEqP3Freq, 4000.0f, "Presence area");
                b.add (pEqP3Gain, 1.0f * k, maxAssistEqDb, "Adds definition");
                b.titleParts.push_back ("tighter");
                break;
            }

            case IntentType::MoreDynamic:
            {
                if (isOn (b.work, pCompOn))
                    b.set (pCompRatio, std::max (1.2f, b.get (pCompRatio) - 1.0f * k), "Less compression lets the music breathe");
                if (isOn (b.work, pLimOn) && b.get (pLimGain) > 0.5f)
                {
                    b.set (pLimGain, std::max (0.0f, b.get (pLimGain) - 2.0f * k), "Less limiter drive restores transients");
                    b.notes.push_back ("This lowers loudness a little - that's the trade-off for more dynamics.");
                }
                if (! isOn (b.work, pCompOn) && ! isOn (b.work, pLimOn))
                    b.notes.push_back ("Nothing in PRIZM is compressing right now - the density is coming from your mix itself.");
                b.titleParts.push_back ("more dynamic");
                break;
            }

            case IntentType::CompSetting:
            {
                const float v = in.plainNumber.value_or (0.0f);
                if (in.module == Module::Limiter && in.compField == CompField::Release)
                {
                    b.set (pLimRelease, v, "Release set as requested");
                    b.titleParts.push_back ("limiter release");
                    break;
                }

                b.enable (pCompOn, "Compressor enabled for this setting");
                switch (in.compField)
                {
                    case CompField::Threshold: b.set (pCompThresh, v > 0 ? -v : v, "Threshold set as requested"); break;
                    case CompField::Ratio:     b.set (pCompRatio, v, "Ratio set as requested"); break;
                    case CompField::Attack:    b.set (pCompAttack, v, "Attack set as requested"); break;
                    case CompField::Release:   b.set (pCompRelease, v, "Release set as requested"); break;
                    case CompField::Makeup:    b.set (pCompMakeup, v, "Makeup gain set as requested"); break;
                    case CompField::Knee:      b.set (pCompKnee, v, "Knee set as requested"); break;
                    default: break;
                }
                b.titleParts.push_back ("compressor settings");
                break;
            }

            case IntentType::Louder:
            {
                if (dir > 0)
                {
                    float step = in.gainDb ? std::abs (*in.gainDb) : 2.0f * k;
                    if (! isOn (b.work, pLimOn))
                    {
                        b.set (pLimOn, 1.0f, "The limiter makes the track louder while keeping peaks under the ceiling");
                        b.set (pLimTruePeak, 1.0f, "True-peak safe");
                        step = in.gainDb ? step : 3.0f * k;
                    }
                    if (b.ctx.live.limGrDb > 6.0f)
                    {
                        step *= 0.5f;
                        b.notes.push_back (fmt ("The limiter is already working hard (%.1f dB) - pushing further costs punch, so I kept this step small.", b.ctx.live.limGrDb));
                    }

                    const float wanted = b.get (pLimGain) + step;
                    const float drive = std::min (wanted, maxAssistLimiterDrive);
                    if (drive < wanted)
                        b.notes.push_back ("Safety: limiter drive capped at 12 dB to avoid audible distortion.");
                    b.set (pLimGain, drive, fmt ("+%.1f dB into the limiter", step));
                    if (b.ctx.live.outputShortTermLufs > -60.0f)
                        b.notes.push_back (fmt ("Currently %.1f LUFS short-term. For an exact target, say e.g. \"make it -14 LUFS\".", b.ctx.live.outputShortTermLufs));
                    b.titleParts.push_back ("louder");
                }
                else
                {
                    const float step = in.gainDb ? std::abs (*in.gainDb) : 2.0f * k;
                    if (isOn (b.work, pLimOn) && b.get (pLimGain) > 0.05f)
                        b.set (pLimGain, std::max (0.0f, b.get (pLimGain) - step), fmt ("-%.1f dB into the limiter", step));
                    else
                        b.set (pOutGain, b.get (pOutGain) - step, fmt ("Output %.1f dB quieter", step));
                    b.titleParts.push_back ("quieter");
                }
                break;
            }

            case IntentType::Ceiling:
            {
                b.enable (pLimOn, "Limiter enforces the ceiling");
                b.set (pLimCeiling, in.gainDb.value_or (-1.0f), "Ceiling set as requested");
                b.set (pLimTruePeak, 1.0f, "True-peak detection so the ceiling holds after D/A and encoding");
                b.titleParts.push_back ("ceiling");
                break;
            }

            case IntentType::Saturate:
            {
                if (dir < 0)
                {
                    if (isOn (b.work, pSatOn))
                    {
                        const float drive = b.get (pSatDrive) - 3.0f * k;
                        if (drive < 1.0f) b.set (pSatOn, 0.0f, "Saturation removed");
                        else              b.set (pSatDrive, drive, "Less drive, cleaner sound");
                    }
                    b.titleParts.push_back ("less saturation");
                    break;
                }

                const bool wasOn = isOn (b.work, pSatOn);
                b.set (pSatOn, 1.0f, "Harmonic saturation for warmth and density");
                if (in.saturationType >= 0)
                    b.set (pSatType, (float) in.saturationType, in.saturationType == 1 ? "Tube: adds even harmonics (warm, rich)"
                                                              : in.saturationType == 2 ? "Soft clip: firmer peak rounding"
                                                                                       : "Tape: smooth odd harmonics");
                if (in.gainDb)
                    b.set (pSatDrive, *in.gainDb, "Drive set as requested");
                else
                {
                    const float drive = wasOn ? b.get (pSatDrive) + 3.0f * k : 4.0f + 2.0f * k;
                    b.set (pSatDrive, std::min (drive, maxAssistSatDrive), "Moderate drive");
                }
                if (in.percent)
                    b.set (pSatMix, *in.percent, "Mix set as requested");
                else if (! wasOn)
                    b.set (pSatMix, 35.0f, "Parallel mix keeps transients intact");
                b.titleParts.push_back ("saturation");
                break;
            }

            case IntentType::Vintage:
            {
                b.set (pSatOn, 1.0f, "Tape saturation for vintage character");
                b.set (pSatType, 0.0f, "Tape");
                b.set (pSatDrive, std::min (6.0f * k, maxAssistSatDrive), "Noticeable but tasteful tape drive");
                b.set (pSatMix, 40.0f, "Blended in parallel");
                b.enable (pEqOn, "EQ needed for this change");
                b.add (pEqLsGain, 1.0f * k, maxAssistEqDb, "Vintage low-end warmth");
                b.set (pEqHsFreq, 9000.0f, "Gentle high roll-off region");
                b.add (pEqHsGain, -1.5f * k, maxAssistEqDb, "Rolled-off top like vintage tape and vinyl");
                b.titleParts.push_back ("vintage vibe");
                break;
            }

            case IntentType::Telephone:
            {
                b.enable (pEqOn, "EQ needed for this effect");
                b.set (pEqHpfOn, 1.0f, "Band-limits the low end like a small speaker");
                b.set (pEqHpfFreq, 400.0f, "Telephone-style low cut");
                b.set (pEqLpfOn, 1.0f, "Band-limits the highs");
                b.set (pEqLpfFreq, 3200.0f, "Telephone-style high cut");
                b.set (pEqP2Freq, 1500.0f, "Nasal mid focus");
                b.set (pEqP2Gain, 3.0f, "Nasal mid focus");
                b.set (pEqP2Q, 2.0f, "Nasal mid focus");
                b.set (pSatOn, 1.0f, "Lo-fi grit");
                b.set (pSatType, 1.0f, "Tube grit");
                b.set (pSatDrive, 12.0f, "Lo-fi grit");
                b.set (pSatMix, 60.0f, "Lo-fi grit");
                b.notes.push_back ("This is a creative lo-fi effect, not a mastering move. Say \"undo\" to remove it after applying.");
                b.titleParts.push_back ("telephone / radio effect");
                break;
            }

            case IntentType::Wider:
            {
                float width = b.get (pImgWidth);
                if (in.percent)
                    width = *in.percent;
                else
                    width += 15.0f * k * dir;

                const float correlation = a != nullptr ? a->correlation : b.ctx.live.correlation;
                if (dir > 0 && ! in.percent && correlation < 0.2f)
                {
                    b.notes.push_back (fmt ("Correlation is already low (%.2f): widening further risks mono cancellation, so I left the width alone.", correlation));
                    break;
                }
                if (! in.percent && width > maxAssistWidth)
                {
                    width = maxAssistWidth;
                    b.notes.push_back ("Safety: width limited to 160% to protect mono compatibility.");
                }

                b.set (pImgOn, 1.0f, "Stereo imager");
                b.set (pImgWidth, width, dir > 0 ? "Wider stereo image (side signal up)" : "Narrower, more focused image");
                if (dir > 0 && a != nullptr && a->lowSideToMidDb > -12.0f && ! isOn (b.work, pImgMonoOn))
                {
                    b.set (pImgMonoOn, 1.0f, "Keeps the bass mono while widening the rest");
                    b.set (pImgMonoFreq, 120.0f, "Mono below 120 Hz");
                }
                b.titleParts.push_back (describeDirection (dir, "wider", "narrower"));
                break;
            }

            case IntentType::MonoBass:
            {
                if (dir < 0)
                {
                    b.set (pImgMonoOn, 0.0f, "Mono bass off");
                    b.titleParts.push_back ("stereo bass");
                    break;
                }
                b.set (pImgOn, 1.0f, "Stereo imager");
                b.set (pImgMonoOn, 1.0f, "Collapses low frequencies to mono for a solid, phase-safe low end");
                b.set (pImgMonoFreq, in.freqHz.value_or (120.0f), "Mono below this frequency");
                b.titleParts.push_back ("mono bass");
                break;
            }

            case IntentType::EqBand:
            {
                const float f = in.freqHz.value_or (1000.0f);
                const float gain = in.gainDb ? *in.gainDb : 2.0f * k * dir;
                const bool userGain = in.gainDb.has_value();
                b.enable (pEqOn, "EQ needed for this change");

                int band;
                if (f < 90.0f)        band = pEqLsFreq;
                else if (f > 10000.0f) band = pEqHsFreq;
                else if (f < 500.0f)  band = pEqP1Freq;
                else if (f < 2500.0f) band = pEqP2Freq;
                else                  band = pEqP3Freq;

                const std::string why = fmt ("%s %s at %s", gain >= 0 ? "Boost" : "Cut", formatParamValue (band + 1, gain).c_str(),
                                             formatParamValue (band, f).c_str());
                b.set (band, f, why);
                if (userGain)
                    b.set (band + 1, gain, why);
                else
                    b.add (band + 1, gain, maxAssistEqDb, why);
                if (band != pEqLsFreq && band != pEqHsFreq)
                    b.set (band + 2, gain < 0 ? 1.5f : 1.0f, gain < 0 ? "Narrower for a cut" : "Broad for a boost");
                b.titleParts.push_back (fmt ("%s %s", gain >= 0 ? "boost" : "cut", formatParamValue (band, f).c_str()));
                break;
            }

            case IntentType::LowCut:
            {
                if (dir < 0) { b.set (pEqHpfOn, 0.0f, "Low cut off"); b.titleParts.push_back ("low cut off"); break; }
                b.enable (pEqOn, "EQ needed for this change");
                b.set (pEqHpfOn, 1.0f, "Removes inaudible rumble that wastes headroom");
                b.set (pEqHpfFreq, in.freqHz.value_or (30.0f), "24 dB/oct low cut");
                b.titleParts.push_back ("low cut");
                break;
            }

            case IntentType::HighCut:
            {
                if (dir < 0) { b.set (pEqLpfOn, 0.0f, "High cut off"); b.titleParts.push_back ("high cut off"); break; }
                b.enable (pEqOn, "EQ needed for this change");
                b.set (pEqLpfOn, 1.0f, "Rolls off the extreme top end");
                b.set (pEqLpfFreq, in.freqHz.value_or (18000.0f), "24 dB/oct high cut");
                b.titleParts.push_back ("high cut");
                break;
            }

            case IntentType::ModuleOn:
            case IntentType::ModuleOff:
            {
                const int p = moduleEnableParam (in.module);
                if (p < 0)
                    break;
                const bool on = in.type == IntentType::ModuleOn;
                b.set (p, on ? 1.0f : 0.0f, fmt ("%s %s", moduleName (in.module), on ? "enabled" : "bypassed"));
                if (on && in.module == Module::Limiter && b.get (pLimGain) < 0.05f)
                    b.notes.push_back ("The limiter will only catch peaks above the ceiling. Say \"louder\" or \"-14 LUFS\" to drive it.");
                b.titleParts.push_back (fmt ("%s %s", moduleName (in.module), on ? "on" : "off"));
                break;
            }

            default:
                break;
        }
    }

    bool isTweak (IntentType t)
    {
        switch (t)
        {
            case IntentType::Warmth: case IntentType::Brightness: case IntentType::Air: case IntentType::Bass:
            case IntentType::Sub: case IntentType::Mud: case IntentType::Boxy: case IntentType::Harsh:
            case IntentType::Presence: case IntentType::Sibilance: case IntentType::Punch: case IntentType::Glue:
            case IntentType::Tight: case IntentType::MoreDynamic: case IntentType::CompSetting: case IntentType::Louder:
            case IntentType::Ceiling: case IntentType::Saturate: case IntentType::Vintage: case IntentType::Telephone:
            case IntentType::Wider: case IntentType::MonoBass: case IntentType::EqBand: case IntentType::LowCut:
            case IntentType::HighCut: case IntentType::ModuleOn: case IntentType::ModuleOff:
                return true;
            default:
                return false;
        }
    }

    std::string joinTitle (const std::vector<std::string>& parts)
    {
        std::string s;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
                s += i + 1 == parts.size() ? " & " : ", ";
            s += parts[i];
        }
        if (! s.empty())
            s[0] = (char) std::toupper ((unsigned char) s[0]);
        return s;
    }

    /** Style nudges for "master it warm and punchy" style requests. */
    void applyStyle (MasterRequest& r, const ParsedMessage& msg)
    {
        for (auto& i : msg.intents)
        {
            const float v = dsp::clampValue (i.direction * i.intensity * 0.67f, -1.0f, 1.0f);
            switch (i.type)
            {
                case IntentType::Warmth: case IntentType::Bass: case IntentType::Sub: r.warmth = v; break;
                case IntentType::Brightness: case IntentType::Air: case IntentType::Presence: r.brightness = v; break;
                case IntentType::Punch: case IntentType::Tight: r.punch = std::max (0.0f, v); break;
                case IntentType::Wider: r.width = v; break;
                case IntentType::Saturate: case IntentType::Vintage: r.saturation = std::max (0.0f, v); break;
                default: break;
            }
        }
    }

    std::string statusText (const AssistantContext& ctx)
    {
        const auto& l = ctx.live;
        if (! l.audioPlaying && l.outputIntegratedLufs <= -100.0f)
            return "I'm not hearing any audio right now. Press play in your DAW and ask again.";

        std::string s = "**Current levels (output):**\n";
        s += fmt ("- Short-term %.1f LUFS, integrated %.1f LUFS (target %.0f)\n", l.outputShortTermLufs, l.outputIntegratedLufs, ctx.targetLufs);
        s += fmt ("- True peak (max) %.1f dBTP%s\n", l.outputTruePeakDb, l.outputTruePeakDb > ctx.ceilingDb + 0.1f ? " - above your ceiling" : "");
        s += fmt ("- Gain reduction: compressor %.1f dB, limiter %.1f dB\n", l.compGrDb, l.limGrDb);
        s += fmt ("- Stereo correlation %+.2f%s", l.correlation, l.correlation < 0.0f ? " - phase problem, check in mono" : "");
        return s;
    }
}

//==============================================================================
std::string welcomeText()
{
    return "Hi! I'm **PRIZM**, your mixing & mastering assistant. Play your track, then try:\n"
           "- **master it for Spotify** - a full chain proposal\n"
           "- **make it warmer**, **less harsh**, **add some vintage vibe**\n"
           "- **load reference**, then **match the reference**\n"
           "- **analyze my mix** - **undo** - **compare** - **help**\n"
           "I never change your sound without asking: every suggestion comes with **Apply** and **Dismiss**.";
}

std::string helpText()
{
    return "**Here's what I can do:**\n"
           "- **Mastering:** \"master it\", \"master for Spotify / YouTube / club\", \"make it -12 LUFS\", \"ceiling -1 dB\"\n"
           "- **Reference:** \"load reference\", \"compare with the reference\", \"match the reference\"\n"
           "- **Tone:** warmer, brighter, more air, more/less bass, less muddy / boxy / harsh, more presence\n"
           "- **EQ:** \"boost 3k by 2 dB\", \"cut 250 Hz\", \"low cut at 30 Hz\"\n"
           "- **Dynamics:** punchier, smoother / glue, tighter, more dynamic, \"ratio 3:1\", \"attack 10 ms\"\n"
           "- **Colour & width:** saturation / tape / tube, vintage, telephone effect, wider, narrower, mono bass\n"
           "- **Session:** undo, redo, reset, compare (A/B with the original), mono check, analyze my mix\n"
           "Add \"a little\" or \"much\" to control how strong a change is.";
}

Reply respond (const std::string& message, const AssistantContext& ctx)
{
    Reply reply;
    const auto msg = parseMessage (message);

    // Session commands first.
    if (msg.has (IntentType::Apply))
    {
        reply.action = ActionType::ApplyPending;
        if (! ctx.hasPendingProposal)
            reply.text = "There's nothing waiting for approval right now. Ask me for a change first.";
        return reply;
    }

    if (msg.has (IntentType::Dismiss))
    {
        reply.action = ActionType::DismissPending;
        reply.text = ctx.hasPendingProposal ? "No problem - I've discarded that suggestion. Your sound is unchanged."
                                            : "Okay. Nothing was pending.";
        return reply;
    }

    if (msg.has (IntentType::Undo))
    {
        reply.action = ActionType::Undo;
        if (! ctx.canUndo)
            reply.text = "There's nothing to undo yet.";
        return reply;
    }

    if (msg.has (IntentType::Redo))
    {
        reply.action = ActionType::Redo;
        if (! ctx.canRedo)
            reply.text = "There's nothing to redo.";
        return reply;
    }

    if (msg.has (IntentType::Reset))
    {
        Proposal p;
        p.title = "Reset to neutral";
        p.summary = "Returns every module to its neutral default (EQ flat, dynamics, saturation, imager and limiter off).";
        const auto defaults = defaultParamState();
        std::vector<std::string> reasons ((size_t) NumParams, "Back to default");
        p.setFromDiff (ctx.params, defaults, reasons);
        if (p.empty())
        {
            reply.text = "Everything is already at its neutral default.";
            return reply;
        }
        p.notes.push_back ("You can undo this afterwards.");
        reply.text = "Here's what a full reset would change. Apply it if you want to start from a clean slate.";
        reply.proposal = p;
        return reply;
    }

    if (msg.has (IntentType::LoadReference))
    {
        reply.action = ActionType::LoadReference;
        reply.text = "Choose a reference track (WAV, AIFF, FLAC, OGG or MP3). I'll analyse it in the background - it is never mixed into your audio.";
        return reply;
    }

    if (msg.has (IntentType::MatchReference))
    {
        if (ctx.reference == nullptr || ! ctx.reference->valid)
        {
            reply.action = ActionType::LoadReference;
            reply.text = "I need a reference track first. Choose one and then ask me to match it again.";
            return reply;
        }

        reply.action = ActionType::RunMaster;
        reply.master.matchReference = true;
        reply.master.targetLufs = ctx.reference->integratedLufs;
        applyStyle (reply.master, msg);
        reply.text = fmt ("Matching **%s**: tonal balance, loudness (%.1f LUFS), dynamics and width. Keep your track playing...",
                          ctx.referenceName.c_str(), ctx.reference->integratedLufs);
        return reply;
    }

    const auto* loudness = msg.find (IntentType::LoudnessTarget);

    if (msg.has (IntentType::Master))
    {
        reply.action = ActionType::RunMaster;
        reply.master.targetLufs = loudness && loudness->lufs ? *loudness->lufs : ctx.targetLufs;
        reply.master.ceilingDb = loudness ? loudness->ceilingDb : ctx.ceilingDb;
        reply.master.platform = loudness ? loudness->platform : "";
        applyStyle (reply.master, msg);
        reply.text = fmt ("Building a mastering chain for **%.0f LUFS**%s. Keep your track playing (the loudest section is best)...",
                          reply.master.targetLufs, reply.master.platform.empty() ? "" : fmt (" (%s)", reply.master.platform.c_str()).c_str());
        return reply;
    }

    if (loudness != nullptr && loudness->lufs)
    {
        reply.action = ActionType::RunMaster;
        reply.master.loudnessOnly = true;
        reply.master.targetLufs = dsp::clampValue (*loudness->lufs, -30.0f, -5.0f);
        reply.master.ceilingDb = loudness->ceilingDb;
        reply.master.platform = loudness->platform;
        reply.text = fmt ("Finding the limiter setting that lands your track at **%.1f LUFS**%s by simulating the chain on your audio...",
                          reply.master.targetLufs, loudness->platform.empty() ? "" : fmt (" for %s", loudness->platform.c_str()).c_str());
        return reply;
    }

    if (msg.has (IntentType::Analyze))
    {
        reply.action = ActionType::RunAnalysis;
        reply.text = ctx.reference != nullptr ? "Analysing your track and comparing it with the reference..." : "Analysing your track...";
        return reply;
    }

    if (msg.has (IntentType::Compare))
    {
        reply.action = ActionType::ToggleCompare;
        return reply;
    }

    if (msg.has (IntentType::MonoCheck))
    {
        reply.action = ActionType::ToggleMonoCheck;
        return reply;
    }

    // Parameter tweaks -> proposal
    Builder b (ctx);
    for (auto& intent : msg.intents)
        if (isTweak (intent.type))
            handleTweak (b, intent);

    if (! b.titleParts.empty())
    {
        Proposal p;
        p.title = joinTitle (b.titleParts);
        p.setFromDiff (ctx.params, b.work, b.reasons);
        p.notes = b.notes;

        if (p.empty())
        {
            reply.text = b.notes.empty() ? "Those settings are already in place - nothing to change."
                                         : b.notes.front();
            return reply;
        }

        reply.text = fmt ("Here's my suggestion for **%s**. Nothing changes until you press **Apply** (or say \"yes\").", p.title.c_str());
        reply.proposal = p;
        return reply;
    }

    if (msg.has (IntentType::Status))   { reply.text = statusText (ctx); return reply; }
    if (msg.has (IntentType::Help))     { reply.text = helpText(); return reply; }
    if (msg.has (IntentType::Explain))
    {
        reply.text = ctx.lastAppliedTitle.empty()
                         ? "I haven't applied anything yet. Each suggestion card lists every parameter with its reason."
                         : fmt ("The last change I applied was **%s**. Scroll up to its card to see every parameter and why it changed, or say \"undo\" to revert it.",
                                ctx.lastAppliedTitle.c_str());
        return reply;
    }
    if (msg.has (IntentType::Greeting)) { reply.text = "Hey! Play your track and tell me how you'd like it to sound - or say **master it** for a full chain."; return reply; }
    if (msg.has (IntentType::Thanks))   { reply.text = "You're welcome - happy mixing!"; return reply; }

    reply.text = "I'm not sure what you'd like to change. Try \"make it warmer\", \"less harsh\", \"master it for Spotify\" or \"help\".";
    return reply;
}

} // namespace prizm::assistant
