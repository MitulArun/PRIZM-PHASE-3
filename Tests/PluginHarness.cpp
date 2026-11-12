/*
  ==============================================================================

    PRIZM V2 - end-to-end harness for the real plugin code (no DAW needed).

    Links the plugin's shared code (Builds/LinuxMakefile/build/prizmv1.a) and
    runs it the way a host would: a separate "audio thread" calls
    processBlock() continuously while the message thread runs the editor,
    timers, chat session and background worker. It then drives real user
    flows and checks the results:

      - chat proposal -> Apply -> parameter changed -> Undo / Redo
      - "master it for Spotify": listen, simulate, propose, apply; then the
        measured output loudness / true peak are checked against the targets
      - reference track loading (a generated WAV) and the reference panel
      - plugin state save / restore (parameters + reference analysis)
      - host bypass latency
      - editor screenshots (PNG) for visual review

    Build & run: Tests/run_plugin_harness.sh [output-dir]

  ==============================================================================
*/

#include <JuceHeader.h>
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <random>
#include <thread>

using namespace prizm;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    /** A synthetic "mix": pink-ish noise bed, a 55 Hz kick-like pulse, a mid
        "vocal" tone with vibrato, some stereo. Roughly -20 LUFS. */
    struct MixGenerator
    {
        double fs = 48000.0;
        double t = 0.0, phase = 0.0;
        std::mt19937 rng { 3 };
        std::normal_distribution<float> n { 0.0f, 1.0f };
        double b[7] {};

        void render (float* l, float* r, int num)
        {
            for (int i = 0; i < num; ++i, t += 1.0 / fs)
            {
                const double w = n (rng);
                b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759;
                b[2] = 0.96900 * b[2] + w * 0.1538520; b[3] = 0.86650 * b[3] + w * 0.3104856;
                b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
                const double pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
                b[6] = w * 0.115926;

                const double beat = std::fmod (t, 0.5);
                const double kick = std::exp (-beat * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat);
                // Vibrato via an accumulated phase (stationary over time).
                const double vib = 1.0 + 0.004 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t);
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 * vib / fs;
                const double vocal = 0.05 * std::sin (phase)
                                   * (0.6 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.3 * t));
                const double side = n (rng) * 0.01;

                l[i] = (float) (0.05 * pink + 0.25 * kick + vocal + side);
                r[i] = (float) (0.05 * pink + 0.25 * kick + vocal * 0.8 - side);
            }
        }
    };

    void writeReferenceWav (const juce::File& file, double fs, double seconds)
    {
        // A brighter, louder, wider "reference" with the same musical layout.
        MixGenerator gen;
        gen.fs = fs;
        juce::AudioBuffer<float> buf (2, (int) (fs * seconds));
        gen.render (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples());

        // Brighten with a simple first-order high-pass blend, widen, and make louder.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            float prev = 0.0f;
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float hp = d[i] - prev;
                prev = d[i];
                d[i] = (d[i] + 2.5f * hp) * 2.2f;
            }
        }
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * 3.0f;
            l[i] = juce::jlimit (-0.98f, 0.98f, m + s);
            r[i] = juce::jlimit (-0.98f, 0.98f, m - s);
        }

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs)
                                                                                   .withNumChannels (2)
                                                                                   .withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    void snapshot (juce::Component& c, const juce::File& file)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::cout << "  snapshot: " << file.getFullPathName() << std::endl;
    }

    const ChatMessage* lastCard (ChatSession& chat)
    {
        const auto& msgs = chat.getMessages();
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
            if (it->proposal)
                return &*it;
        return nullptr;
    }

    /** Depth-first search of the real component tree. */
    template <typename T>
    void findAll (juce::Component& root, std::function<bool (T&)> pred, std::vector<T*>& out)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                if (pred (*t))
                    out.push_back (t);
            findAll<T> (*c, pred, out);
        }
    }
}

//==============================================================================
class Harness : private juce::Timer
{
public:
    Harness (const juce::File& outDir) : dir (outDir)
    {
        processor.reset (dynamic_cast<Prizmv1AudioProcessor*> (createPluginFilter()));
        processor->setPlayConfigDetails (2, 2, fs, block);
        processor->prepareToPlay (fs, block);

        editor.reset (processor->createEditor());
        editor->setVisible (true);
        editor->setSize (1240, 820);

        audioThread = std::thread ([this] { audioLoop(); });
        startTimer (100);
    }

    ~Harness() override
    {
        stopTimer();
        running = false;
        if (audioThread.joinable())
            audioThread.join();
        editor.reset();
        processor.reset();
    }

private:
    void audioLoop()
    {
        outputMeter.prepare (fs);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        MixGenerator gen;
        gen.fs = fs;
        const auto blockMs = 1000.0 * block / fs;

        while (running)
        {
            gen.render (buffer.getWritePointer (0), buffer.getWritePointer (1), block);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            processor->processBlock (buffer, midi);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            totalProcessMs += elapsed;
            ++blocksProcessed;

            // What actually leaves the plugin (after the A/B / mono monitor stage).
            outputMeter.process (buffer.getReadPointer (0), buffer.getReadPointer (1), block);
            outputShortTerm = outputMeter.getShortTerm();
            float diff = 0.0f;
            for (int i = 0; i < block; ++i)
                diff = std::max (diff, std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
            maxLRDifference = diff;

            if (measureOutput)
                for (int ch = 0; ch < 2; ++ch)
                    maxOutput = std::max (maxOutput.load(), buffer.getMagnitude (ch, 0, block));

            // Run at ~4x real time.
            const auto sleep = blockMs / 4.0 - elapsed;
            if (sleep > 0.5)
                std::this_thread::sleep_for (std::chrono::microseconds ((int) (sleep * 1000.0)));
        }
    }

    double seconds() const { return (juce::Time::getMillisecondCounter() - stepStart) / 1000.0; }
    void next() { ++step; stepStart = juce::Time::getMillisecondCounter(); }

    std::vector<juce::Button*> buttons (std::function<bool (juce::Button&)> pred)
    {
        std::vector<juce::Button*> out;
        findAll<juce::Button> (*editor, pred, out);
        return out;
    }

    void timerCallback() override
    {
        auto& chat = processor->getChat();
        auto& history = processor->getHistory();

        switch (step)
        {
            case 0:
                std::cout << "\n== Chat proposal -> apply -> undo/redo" << std::endl;
                chat.sendUserMessage ("make it a little warmer but less harsh");
                next();
                break;

            case 1:
            {
                const auto* card = lastCard (chat);
                check (card != nullptr && card->cardState == ChatMessage::CardState::Pending, "tweak request produced a pending proposal");
                check (std::abs (processor->getParamState()[pEqLsGain]) < 1.0e-4f, "nothing changed before approval");
                if (card != nullptr)
                {
                    const int id = card->id;
                    chat.applyProposal (id);
                }
                next();
                break;
            }

            case 2:
            {
                const auto p = processor->getParamState();
                check (p[pEqLsGain] > 0.5f, "Apply changed the real low-shelf parameter (" + juce::String (p[pEqLsGain], 2) + " dB)");
                check (p[pEqP3Gain] < -0.5f, "Apply cut the harsh band (" + juce::String (p[pEqP3Gain], 2) + " dB)");
                check (history.canUndo(), "an undo point was recorded");
                chat.sendUserMessage ("undo");
                check (std::abs (processor->getParamState()[pEqLsGain]) < 1.0e-4f, "undo restored the previous settings");
                chat.sendUserMessage ("redo");
                check (processor->getParamState()[pEqLsGain] > 0.5f, "redo re-applied the change");
                history.undo();
                next();
                break;
            }

            case 3:
                std::cout << "\n== Mastering assistant (master it for Spotify)" << std::endl;
                chat.sendUserMessage ("master it for spotify");
                next();
                break;

            case 4:
            {
                if (chat.isBusy() && seconds() < 90.0)
                {
                    if (seconds() > 1.0 && ! shotListening)
                    {
                        snapshot (*editor, dir.getChildFile ("prizm_v2_listening.png"));
                        shotListening = true;
                    }
                    break;
                }

                const auto* card = lastCard (chat);
                check (card != nullptr && card->cardState == ChatMessage::CardState::Pending, "mastering proposal arrived and awaits approval");
                if (card != nullptr && card->proposal)
                {
                    const auto& pr = card->proposal->prediction;
                    std::cout << "  proposal: " << card->proposal->title << ", " << card->proposal->changes.size()
                              << " changes, predicted " << pr.integratedLufs << " LUFS / " << pr.truePeakDb << " dBTP" << std::endl;
                    check (pr.valid && std::abs (pr.integratedLufs + 14.0f) < 0.5f, "prediction hits -14 LUFS");
                }

                snapshot (*editor, dir.getChildFile ("prizm_v2_proposal.png"));

                if (card != nullptr)
                {
                    const int id = card->id;
                    chat.applyProposal (id);
                }
                processor->resetLoudnessMeters();
                measureOutput = true;
                maxOutput = 0.0f;
                next();
                break;
            }

            case 5:
            {
                if (seconds() < 20.0)   // ~80 s of audio at 4x: lets integrated loudness settle
                    break;

                const auto m = processor->getMeters();
                std::cout << "  measured output: integrated " << m.outIntegrated << " LUFS, true-peak max " << m.outTruePeakMaxDb
                          << " dBTP, sample peak " << juce::Decibels::gainToDecibels (maxOutput.load()) << " dBFS, limiter "
                          << m.limGrDb << " dB" << std::endl;
                check (std::abs (m.outIntegrated + 14.0f) < 1.0f, "live output integrated loudness is within 1 LU of -14 LUFS");
                check (m.outTruePeakMaxDb <= -1.0f + 0.3f, "live output true peak respects the -1 dBTP ceiling");
                check (juce::Decibels::gainToDecibels (maxOutput.load()) <= -1.0f + 0.01f, "no sample exceeds the ceiling");
                snapshot (*editor, dir.getChildFile ("prizm_v2_mastered.png"));
                measureOutput = false;

                std::cout << "\n== A/B compare, level match, mono check" << std::endl;
                processedShortTerm = m.outShortTerm;
                chat.sendUserMessage ("compare");
                check (processor->isComparingOriginal(), "chat \"compare\" switches to the original");
                next();
                break;
            }

            case 6:
            {
                if (seconds() < 4.0)   // 16 s of audio: short-term loudness of what is heard settles
                    break;

                const float heard = outputShortTerm.load();
                std::cout << "  processed " << processedShortTerm << " LUFS short-term; original as heard (level-matched) "
                          << heard << " LUFS" << std::endl;
                check (std::abs (heard - processedShortTerm) < 1.0f, "level-matched A/B: the original plays at the processed loudness");

                chat.sendUserMessage ("compare");
                check (! processor->isComparingOriginal(), "chat \"compare\" again returns to the processed sound");
                chat.sendUserMessage ("mono check");
                check (processor->isMonoCheck(), "chat \"mono check\" enables mono monitoring");
                next();
                break;
            }

            case 7:
                if (seconds() < 0.5)
                    break;
                check (maxLRDifference.load() < 1.0e-6f, "mono check makes left and right identical at the output");
                processor->setMonoCheck (false);
                next();
                break;

            case 8:
            {
                std::cout << "\n== Reference track" << std::endl;
                const auto wav = dir.getChildFile ("prizm_test_reference.wav");
                writeReferenceWav (wav, 44100.0, 30.0);
                chat.loadReference (wav);
                next();
                break;
            }

            case 9:
            {
                if (seconds() > 90.0)
                {
                    check (false, "reference + track analysis finished in time");
                    next();
                    break;
                }

                if (chat.isBusy())
                    break;

                if (! processor->getReference().valid)
                    break;

                if (chat.getRecentInputAnalysis() == nullptr || ! analysisRequested)
                {
                    if (! analysisRequested)
                    {
                        chat.requestTrackAnalysis (false);
                        analysisRequested = true;
                    }
                    break;
                }

                const auto ref = processor->getReference();
                check (ref.valid, "reference analysed: " + ref.name + juce::String::formatted (" (%.1f LUFS, tilt %.1f dB/oct)",
                                                                                           ref.analysis.integratedLufs, ref.analysis.tiltDbPerOctave));
                check (chat.getRecentInputAnalysis() != nullptr, "your track was analysed for comparison");

                for (auto* b : buttons ([] (juce::Button& b) { return b.getButtonText() == "Reference"; }))
                    b->triggerClick();
                next();
                break;
            }

            case 10:
                if (seconds() < 1.5)
                    break;
                snapshot (*editor, dir.getChildFile ("prizm_v2_reference.png"));
                chat.sendUserMessage ("match the reference");
                next();
                break;

            case 11:
            {
                if (chat.isBusy() && seconds() < 90.0)
                    break;

                // The chat panel picks up new messages asynchronously; give it a UI tick.
                if (! matchArrived)
                {
                    matchArrived = true;
                    stepStart = juce::Time::getMillisecondCounter();
                    break;
                }
                if (seconds() < 0.5)
                    break;

                const auto* card = lastCard (chat);
                check (card != nullptr && card->proposal && card->proposal->title == "Match reference", "match-reference proposal produced");
                if (card == nullptr || ! card->proposal)
                {
                    next();
                    break;
                }

                std::cout << "  match prediction " << card->proposal->prediction.integratedLufs << " LUFS (reference "
                          << processor->getReference().analysis.integratedLufs << ")" << std::endl;

                std::cout << "\n== Proposal card controls (real components)" << std::endl;
                const int id = card->id;
                auto visibleToggles = [this]
                {
                    // Enabled checkboxes belong to the pending card only.
                    return buttons ([] (juce::Button& b) { return b.getTooltip() == "Include this change" && b.isVisible() && b.isEnabled(); });
                };

                auto toggles = visibleToggles();
                check (! toggles.empty(), "card shows per-change checkboxes");
                if (! toggles.empty())   // a real click toggles the box and calls clicked() synchronously
                    toggles.front()->setToggleState (! toggles.front()->getToggleState(), juce::sendNotificationSync);

                const auto* updated = lastCard (chat);
                check (updated != nullptr && updated->proposal && updated->proposal->numSelected() == (int) updated->proposal->changes.size() - 1,
                       "unticking a checkbox deselects that change in the proposal");

                const size_t before = visibleToggles().size();
                auto showAll = buttons ([] (juce::Button& b) { return b.getButtonText().startsWith ("Show all") && b.isVisible(); });
                if (! showAll.empty() && showAll.back()->onClick)
                    showAll.back()->onClick();   // the newest card
                const size_t after = visibleToggles().size();
                check (after > before, "\"Show all\" expands the card (" + juce::String ((int) before) + " -> " + juce::String ((int) after) + " rows)");
                snapshot (*editor, dir.getChildFile ("prizm_v2_match.png"));

                const auto paramsBefore = processor->getParamState();
                for (auto* b : buttons ([] (juce::Button& b) { return b.getButtonText() == "Dismiss" && b.isVisible(); }))
                    if (b->onClick)
                        b->onClick();

                bool dismissed = false;
                for (auto& m : chat.getMessages())
                    if (m.id == id)
                        dismissed = m.cardState == ChatMessage::CardState::Dismissed;
                check (dismissed, "Dismiss button rejects the proposal");
                check (processor->getParamState() == paramsBefore, "dismissing changed nothing");
                next();
                break;
            }

            case 12:
            {
                std::cout << "\n== State save / restore, bypass latency, CPU" << std::endl;
                auto params = processor->getParamState();
                params[pEqP2Gain] = 3.5f;
                history.applyWithUndo (params, "test");

                juce::MemoryBlock state;
                processor->getStateInformation (state);

                std::unique_ptr<Prizmv1AudioProcessor> other (dynamic_cast<Prizmv1AudioProcessor*> (createPluginFilter()));
                other->setStateInformation (state.getData(), (int) state.getSize());
                const auto restored = other->getParamState();
                bool same = true;
                for (int i = 0; i < NumParams; ++i)
                    same = same && std::abs (restored[(size_t) i] - params[(size_t) i]) < 1.0e-3f;
                check (same, "all parameters restored from saved state");
                check (other->getReference().valid && other->getReference().name == processor->getReference().name,
                       "reference analysis restored from saved state (no re-analysis needed)");

                other->setPlayConfigDetails (2, 2, fs, block);
                other->prepareToPlay (fs, block);
                juce::AudioBuffer<float> buf (2, 1024);
                buf.clear();
                buf.setSample (0, 10, 0.5f);
                buf.setSample (1, 10, 0.5f);
                juce::MidiBuffer midi;
                other->processBlockBypassed (buf, midi);
                int peakIndex = 0;
                for (int i = 0; i < 1024; ++i)
                    if (std::abs (buf.getSample (0, i)) > std::abs (buf.getSample (0, peakIndex)))
                        peakIndex = i;
                check (peakIndex == 10 + other->getLatencySamples(),
                       "host bypass is delayed by the reported latency (" + juce::String (other->getLatencySamples()) + " samples)");

                const double avgMs = totalProcessMs / juce::jmax (1, blocksProcessed.load());
                const double budgetMs = 1000.0 * block / fs;
                std::cout << "  processBlock average " << avgMs << " ms per " << block << "-sample block ("
                          << 100.0 * avgMs / budgetMs << "% of real time, this build)" << std::endl;

                snapshot (*editor, dir.getChildFile ("prizm_v2_final.png"));
                next();
                break;
            }

            default:
                std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " CHECK(S) FAILED") << std::endl;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
        }
    }

    const double fs = 48000.0;
    const int block = 512;
    juce::File dir;
    std::unique_ptr<Prizmv1AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audioThread;
    std::atomic<bool> running { true }, measureOutput { false };
    std::atomic<float> maxOutput { 0.0f };
    std::atomic<int> blocksProcessed { 0 };
    double totalProcessMs = 0.0;
    int step = 0;
    juce::uint32 stepStart = juce::Time::getMillisecondCounter();
    bool shotListening = false, analysisRequested = false, matchArrived = false;
    float processedShortTerm = -144.0f;
    dsp::LoudnessMeter outputMeter;
    std::atomic<float> outputShortTerm { -144.0f }, maxLRDifference { 0.0f };
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();

    {
        Harness harness (dir);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return failures == 0 ? 0 : 1;
}
