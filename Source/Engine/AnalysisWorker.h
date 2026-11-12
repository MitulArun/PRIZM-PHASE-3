/*
  ==============================================================================

    PRIZM V2 - background analysis thread.

    Everything expensive happens here, never on the audio thread:
      - streaming + analysing reference files from disk,
      - keeping the last ~20 s of audible input/output audio (fed through
        lock-free FIFOs from the audio thread),
      - building mastering proposals (analysis + offline chain simulation),
      - analysing the user's track on request.

    The reference track is only ever *analysed* - it is never routed to the
    plugin's output, so it can't replace or modify the live audio.

  ==============================================================================
*/

#pragma once

#include "../Assistant/MasteringAssistant.h"
#include "AudioFifo.h"

#include <deque>
#include <optional>

namespace prizm
{

struct WorkerResult
{
    enum class Kind { Reference, Master, Analysis, Failed };

    Kind kind = Kind::Failed;
    int requestId = 0;
    dsp::AnalysisResult analysis;          // Reference: the file; Analysis/Master: the user's track
    assistant::Proposal proposal;          // Master
    juce::String fileName, filePath, error;
    bool analysedOutput = false;
    double secondsUsed = 0.0;
};

struct WorkerStatus
{
    bool busy = false;
    int requestId = 0;
    juce::String message;
    float progress = -1.0f;    // < 0: indeterminate
};

class AnalysisWorker : private juce::Thread
{
public:
    AnalysisWorker();
    ~AnalysisWorker() override;

    /** Any thread. Clears the rolling buffers if the rate changes. */
    void setSampleRate (double sampleRate) noexcept { pendingSampleRate.store (sampleRate); }

    /** Audio thread only: lock-free, drops data if the worker falls behind. */
    void pushAudio (const float* inL, const float* inR, const float* outL, const float* outR, int numSamples) noexcept;

    // Message thread
    int requestReference (const juce::File& file);
    int requestMaster (const assistant::MasterRequest& request, const ParamState& current,
                       std::optional<dsp::AnalysisResult> reference);
    int requestAnalysis (bool processedOutput);
    void cancelAll();

    WorkerStatus getStatus() const;
    bool popResult (WorkerResult& out);

    /** Seconds of audible audio currently held (input side). */
    double getSecondsHeard() const noexcept { return secondsHeard.load(); }

private:
    struct Job
    {
        enum class Type { Reference, Master, Analysis } type;
        int id = 0;
        juce::File file;
        assistant::MasterRequest master;
        ParamState params {};
        std::optional<dsp::AnalysisResult> reference;
        bool output = false;
    };

    void run() override;
    void drainFifos();
    void ensureRollingBuffers();
    bool waitForAudio (int jobId, double secondsNeeded);
    void copyClip (bool output, double maxSeconds, std::vector<float>& left, std::vector<float>& right) const;

    void runReference (const Job& job);
    void runMaster (const Job& job);
    void runAnalysis (const Job& job);

    void setStatus (bool busy, int id, const juce::String& message, float progress = -1.0f);
    void postResult (WorkerResult r);
    bool cancelled() const noexcept { return cancelFlag.load() || threadShouldExit(); }

    // Audio-thread FIFOs (input = before processing, output = processed)
    AudioFifo<2> inputFifo { 1 << 16 }, outputFifo { 1 << 16 };

    // Rolling buffers (worker thread only), interleaved stereo
    std::vector<float> rollIn, rollOut, scratchIn, scratchOut;
    int rollCapacity = 0, rollWrite = 0, rollFilled = 0;
    double rollRate = 0.0;
    juce::uint32 lastAudibleMs = 0;

    std::atomic<double> pendingSampleRate { 0.0 };
    std::atomic<double> secondsHeard { 0.0 };
    std::atomic<bool> cancelFlag { false };

    juce::CriticalSection jobLock, resultLock, statusLock;
    std::deque<Job> jobs;
    std::deque<WorkerResult> results;
    WorkerStatus status;
    int nextId = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisWorker)
};

} // namespace prizm
