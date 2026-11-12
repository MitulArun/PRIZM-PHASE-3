/*
  ==============================================================================

    PRIZM V2 - plugin processor.

    Evolved from the V1 template processor: same class, plugin codes and bus
    layouts, now hosting the real mastering chain, metering, the analysis
    worker, undo history and the chat session.

    Threading:
      - audio thread: processBlock() only touches the DSP chain, atomics and
        lock-free FIFOs (no locks, no allocation, no file or UI work),
      - analysis thread: AnalysisWorker (reference files, proposals),
      - message thread: editor, chat session, history, state.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "DSP/LoudnessMeter.h"
#include "DSP/MasteringChain.h"
#include "Engine/AnalysisWorker.h"
#include "Engine/AudioFifo.h"
#include "Engine/ChatSession.h"
#include "Engine/HistoryManager.h"
#include "Engine/MeterData.h"

//==============================================================================
/**
*/
class Prizmv1AudioProcessor  : public juce::AudioProcessor,
                               public prizm::ParamStateHost
{
public:
    //==============================================================================
    Prizmv1AudioProcessor();
    ~Prizmv1AudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameters
    juce::AudioProcessorValueTreeState apvts;

    prizm::ParamState getParamState() const override;
    void setParamState (const prizm::ParamState& state) override;   // message thread
    juce::RangedAudioParameter* getParam (int index) const noexcept  { return params[(size_t) index]; }

    //==============================================================================
    // Engine
    prizm::HistoryManager& getHistory() noexcept      { return *history; }
    prizm::ChatSession& getChat() noexcept            { return *chat; }
    prizm::AnalysisWorker& getWorker() noexcept       { return worker; }
    prizm::MeterSnapshot getMeters() const noexcept   { return meters.load(); }
    void resetLoudnessMeters() noexcept               { meterResetRequested = true; }

    double getCurrentSampleRate() const noexcept      { return currentSampleRate.load(); }
    int getChainLatencySamples() const noexcept       { return latencySamples.load(); }

    // Monitoring (not automatable, not saved with the session)
    void setCompareOriginal (bool b) noexcept         { compareOriginal = b; }
    bool isComparingOriginal() const noexcept         { return compareOriginal.load(); }
    void setLevelMatch (bool b) noexcept              { levelMatch = b; }
    bool isLevelMatch() const noexcept                { return levelMatch.load(); }
    void setMonoCheck (bool b) noexcept               { monoCheck = b; }
    bool isMonoCheck() const noexcept                 { return monoCheck.load(); }

    // Visualisation taps (read by the editor's timer)
    prizm::AudioFifo<2>& getSpectrumFifo() noexcept   { return spectrumFifo; }   // ch0 = input (mono), ch1 = processed (mono)
    prizm::AudioFifo<2>& getScopeFifo() noexcept      { return scopeFifo; }      // processed L/R

    //==============================================================================
    // Reference track and session targets (message thread; persisted)
    struct ReferenceInfo
    {
        juce::String name, path;
        prizm::dsp::AnalysisResult analysis;
        bool valid = false;
    };

    ReferenceInfo getReference() const;
    void setReference (const ReferenceInfo& info);
    void clearReference();
    int getReferenceVersion() const noexcept          { return referenceVersion.load(); }

    float getTargetLufs() const noexcept              { return targetLufs.load(); }
    void setTargetLufs (float v) noexcept             { targetLufs = v; }
    float getCeilingDb() const noexcept               { return ceilingDb.load(); }
    void setCeilingDb (float v) noexcept              { ceilingDb = v; }

    /** Set when a new session state was loaded (history captures a fresh "original"). */
    std::atomic<bool> sessionStartPending { true };

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void measureInput (const float* l, const float* r, int n) noexcept;
    void measureOutput (const float* l, const float* r, int n) noexcept;

    std::array<juce::RangedAudioParameter*, prizm::NumParams> params {};
    std::array<std::atomic<float>*, prizm::NumParams> rawParams {};

    prizm::dsp::MasteringChain chain;
    prizm::dsp::DelayLine bypassDelay[2];

    // Fixed scratch space so processBlock never allocates, whatever block size the host uses.
    static constexpr int scratchSize = 2048;
    std::vector<float> monoScratch, inCopyL, inCopyR, tapL, tapR, monoIn, monoOut;

    // Metering (audio thread state)
    prizm::dsp::LoudnessMeter inLoudness, outLoudness;
    prizm::dsp::TruePeakDetector truePeak[2];
    float inRmsState[2] {}, outRmsState[2] {};
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0;
    float truePeakMax = 0.0f, matchDb = 0.0f;
    int silentBlocks = 0, peakHoldRemaining = 0;
    prizm::MeterSnapshot snapshot;
    prizm::MeterData meters;
    std::atomic<bool> meterResetRequested { false };

    std::atomic<bool> compareOriginal { false }, levelMatch { true }, monoCheck { false };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> latencySamples { 0 };

    prizm::AudioFifo<2> spectrumFifo { 1 << 15 }, scopeFifo { 1 << 14 };
    prizm::AnalysisWorker worker;

    mutable juce::CriticalSection referenceLock;
    ReferenceInfo reference;
    std::atomic<int> referenceVersion { 0 };
    std::atomic<float> targetLufs { -14.0f }, ceilingDb { -1.0f };

    std::unique_ptr<prizm::HistoryManager> history;
    std::unique_ptr<prizm::ChatSession> chat;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Prizmv1AudioProcessor)
};
