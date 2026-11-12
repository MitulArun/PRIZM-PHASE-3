/*
  ==============================================================================

    PRIZM V2 - background analysis thread.

  ==============================================================================
*/

#include "AnalysisWorker.h"

namespace prizm
{

namespace
{
    constexpr double rollingSeconds = 20.0;
    constexpr double clipSeconds = 15.0;           // what proposals are based on
    constexpr double secondsNeededForMaster = 8.0;
    constexpr double secondsNeededForAnalysis = 6.0;
    constexpr double staleAfterSeconds = 20.0;     // older audio is discarded before a new listen
    constexpr double listenTimeoutSeconds = 120.0;
    constexpr float audibleThresholdDb = -65.0f;
    constexpr int drainChunk = 1024;
    constexpr double maxReferenceSeconds = 20.0 * 60.0;
}

AnalysisWorker::AnalysisWorker() : juce::Thread ("PRIZM analysis")
{
    scratchIn.resize ((size_t) drainChunk * 2);
    scratchOut.resize ((size_t) drainChunk * 2);
    startThread (juce::Thread::Priority::low);
}

AnalysisWorker::~AnalysisWorker()
{
    cancelFlag = true;
    stopThread (4000);
}

void AnalysisWorker::pushAudio (const float* inL, const float* inR, const float* outL, const float* outR, int numSamples) noexcept
{
    const float* in[2] = { inL, inR };
    const float* out[2] = { outL, outR };

    // Keep both FIFOs in lockstep: the producer always writes the same number of
    // frames to both and the worker always reads the same number from both.
    const int n = std::min ({ numSamples, inputFifo.getFreeSpace(), outputFifo.getFreeSpace() });
    if (n <= 0)
        return;

    inputFifo.push (in, n);
    outputFifo.push (out, n);
}

//==============================================================================
int AnalysisWorker::requestReference (const juce::File& file)
{
    const juce::ScopedLock sl (jobLock);
    Job job { Job::Type::Reference };
    job.id = nextId++;
    job.file = file;
    jobs.push_back (job);
    cancelFlag = false;
    notify();
    return job.id;
}

int AnalysisWorker::requestMaster (const assistant::MasterRequest& request, const ParamState& current,
                                   std::optional<dsp::AnalysisResult> reference)
{
    const juce::ScopedLock sl (jobLock);
    Job job { Job::Type::Master };
    job.id = nextId++;
    job.master = request;
    job.params = current;
    job.reference = std::move (reference);
    jobs.push_back (job);
    cancelFlag = false;
    notify();
    return job.id;
}

int AnalysisWorker::requestAnalysis (bool processedOutput)
{
    const juce::ScopedLock sl (jobLock);
    Job job { Job::Type::Analysis };
    job.id = nextId++;
    job.output = processedOutput;
    jobs.push_back (job);
    cancelFlag = false;
    notify();
    return job.id;
}

void AnalysisWorker::cancelAll()
{
    {
        const juce::ScopedLock sl (jobLock);
        jobs.clear();
    }
    cancelFlag = true;
    notify();
}

WorkerStatus AnalysisWorker::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

bool AnalysisWorker::popResult (WorkerResult& out)
{
    const juce::ScopedLock sl (resultLock);
    if (results.empty())
        return false;

    out = std::move (results.front());
    results.pop_front();
    return true;
}

void AnalysisWorker::setStatus (bool busy, int id, const juce::String& message, float progress)
{
    const juce::ScopedLock sl (statusLock);
    status.busy = busy;
    status.requestId = id;
    status.message = message;
    status.progress = progress;
}

void AnalysisWorker::postResult (WorkerResult r)
{
    const juce::ScopedLock sl (resultLock);
    results.push_back (std::move (r));
}

//==============================================================================
void AnalysisWorker::ensureRollingBuffers()
{
    const double rate = pendingSampleRate.load();
    if (rate <= 0.0 || rate == rollRate)
        return;

    rollRate = rate;
    rollCapacity = std::min ((int) (rate * rollingSeconds), 1 << 22);
    rollIn.assign ((size_t) rollCapacity * 2, 0.0f);
    rollOut.assign ((size_t) rollCapacity * 2, 0.0f);
    rollWrite = rollFilled = 0;
    secondsHeard = 0.0;
}

void AnalysisWorker::drainFifos()
{
    ensureRollingBuffers();

    while (true)
    {
        const int ready = std::min (inputFifo.getNumReady(), outputFifo.getNumReady());
        if (ready <= 0)
            break;

        const int n = std::min (ready, drainChunk);
        inputFifo.pop (scratchIn.data(), n);
        outputFifo.pop (scratchOut.data(), n);

        if (rollCapacity <= 0)
            continue;

        double sum = 0.0;
        for (int i = 0; i < n * 2; ++i)
            sum += (double) scratchIn[(size_t) i] * scratchIn[(size_t) i];

        if (dsp::powerToDb (sum / (n * 2)) < audibleThresholdDb)
            continue;   // silence (stopped transport, gaps) is not useful for analysis

        lastAudibleMs = juce::Time::getMillisecondCounter();

        for (int i = 0; i < n; ++i)
        {
            rollIn[(size_t) rollWrite * 2]     = scratchIn[(size_t) i * 2];
            rollIn[(size_t) rollWrite * 2 + 1] = scratchIn[(size_t) i * 2 + 1];
            rollOut[(size_t) rollWrite * 2]     = scratchOut[(size_t) i * 2];
            rollOut[(size_t) rollWrite * 2 + 1] = scratchOut[(size_t) i * 2 + 1];
            rollWrite = (rollWrite + 1) % rollCapacity;
        }

        rollFilled = std::min (rollFilled + n, rollCapacity);
        secondsHeard = rollRate > 0.0 ? rollFilled / rollRate : 0.0;
    }
}

void AnalysisWorker::copyClip (bool output, double maxSeconds, std::vector<float>& left, std::vector<float>& right) const
{
    const int frames = std::min (rollFilled, (int) (maxSeconds * rollRate));
    left.resize ((size_t) frames);
    right.resize ((size_t) frames);
    const auto& src = output ? rollOut : rollIn;

    int pos = (rollWrite - frames + rollCapacity) % std::max (1, rollCapacity);
    for (int i = 0; i < frames; ++i)
    {
        left[(size_t) i] = src[(size_t) pos * 2];
        right[(size_t) i] = src[(size_t) pos * 2 + 1];
        pos = (pos + 1) % rollCapacity;
    }
}

bool AnalysisWorker::waitForAudio (int jobId, double secondsNeeded)
{
    drainFifos();

    const double sinceAudible = (juce::Time::getMillisecondCounter() - lastAudibleMs) / 1000.0;
    if (rollFilled > 0 && sinceAudible > staleAfterSeconds)
    {
        // What we hold is old - listen to the track again from scratch.
        rollWrite = rollFilled = 0;
        secondsHeard = 0.0;
    }

    const auto start = juce::Time::getMillisecondCounter();
    while (secondsHeard.load() < secondsNeeded)
    {
        if (cancelled())
            return false;

        if ((juce::Time::getMillisecondCounter() - start) / 1000.0 > listenTimeoutSeconds)
            return false;

        setStatus (true, jobId,
                   juce::String::formatted ("Listening... %.0f / %.0f s - press play (the loudest part works best)",
                                            secondsHeard.load(), secondsNeeded),
                   (float) (secondsHeard.load() / secondsNeeded));
        wait (50);
        drainFifos();
    }

    return true;
}

//==============================================================================
void AnalysisWorker::run()
{
    // Offline simulations run the same filters as the audio thread; keep decaying
    // tails from turning into slow denormals here too.
    const juce::ScopedNoDenormals noDenormals;

    while (! threadShouldExit())
    {
        drainFifos();

        std::optional<Job> job;
        {
            const juce::ScopedLock sl (jobLock);
            if (! jobs.empty())
            {
                job = jobs.front();
                jobs.pop_front();
            }
        }

        if (! job)
        {
            wait (40);
            continue;
        }

        cancelFlag = false;

        switch (job->type)
        {
            case Job::Type::Reference: runReference (*job); break;
            case Job::Type::Master:    runMaster (*job);    break;
            case Job::Type::Analysis:  runAnalysis (*job);  break;
        }

        setStatus (false, job->id, {});
    }
}

void AnalysisWorker::runReference (const Job& job)
{
    WorkerResult r;
    r.requestId = job.id;
    r.fileName = job.file.getFileName();
    r.filePath = job.file.getFullPathName();
    setStatus (true, job.id, "Reading " + r.fileName + "...", 0.0f);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (job.file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        r.kind = WorkerResult::Kind::Failed;
        r.error = "Couldn't read \"" + r.fileName + "\". Supported formats: " + formats.getWildcardForAllFormats();
        postResult (std::move (r));
        return;
    }

    const auto total = std::min (reader->lengthInSamples, (juce::int64) (maxReferenceSeconds * reader->sampleRate));
    dsp::AudioAnalyzer analyzer;
    analyzer.prepare (reader->sampleRate);

    constexpr int chunk = 32768;
    juce::AudioBuffer<float> buffer (2, chunk);

    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        if (cancelled())
            return;

        const int n = (int) std::min ((juce::int64) chunk, total - pos);
        buffer.clear();
        reader->read (&buffer, 0, n, pos, true, true);

        if (reader->numChannels == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);

        analyzer.process (buffer.getReadPointer (0), buffer.getReadPointer (1), n);
        setStatus (true, job.id, "Analysing reference: " + r.fileName, (float) ((double) pos / (double) total));

        drainFifos();   // keep the rolling buffers fed during long files
    }

    r.analysis = analyzer.finish();
    r.secondsUsed = (double) total / reader->sampleRate;

    if (! r.analysis.valid)
    {
        r.kind = WorkerResult::Kind::Failed;
        r.error = "\"" + r.fileName + "\" seems to be silent or too short to analyse.";
    }
    else
    {
        r.kind = WorkerResult::Kind::Reference;
    }

    postResult (std::move (r));
}

void AnalysisWorker::runMaster (const Job& job)
{
    WorkerResult r;
    r.requestId = job.id;

    if (! waitForAudio (job.id, secondsNeededForMaster))
    {
        if (cancelled())
            return;
        r.kind = WorkerResult::Kind::Failed;
        r.error = "I didn't hear enough audio to work with. Press play in your DAW (ideally the loudest section) and ask again.";
        postResult (std::move (r));
        return;
    }

    setStatus (true, job.id, "Analysing your track...");
    std::vector<float> left, right;
    copyClip (false, clipSeconds, left, right);

    dsp::AudioAnalyzer analyzer;
    analyzer.prepare (rollRate);
    analyzer.process (left.data(), right.data(), (int) left.size());
    r.analysis = analyzer.finish();
    r.secondsUsed = left.size() / rollRate;

    if (! r.analysis.valid)
    {
        r.kind = WorkerResult::Kind::Failed;
        r.error = "The audio I heard was too quiet to analyse.";
        postResult (std::move (r));
        return;
    }

    setStatus (true, job.id, job.master.loudnessOnly ? "Simulating the limiter on your audio..."
                                                     : "Simulating the proposed chain on your audio...");

    assistant::AudioClip clip { left.data(), right.data(), (int) left.size(), rollRate };
    const dsp::AnalysisResult* ref = job.reference ? &*job.reference : nullptr;

    r.proposal = assistant::buildMasteringProposal (job.master, job.params, r.analysis, ref, clip,
                                                    [this] { return cancelled(); });
    if (cancelled())
        return;

    r.kind = WorkerResult::Kind::Master;
    postResult (std::move (r));
}

void AnalysisWorker::runAnalysis (const Job& job)
{
    WorkerResult r;
    r.requestId = job.id;
    r.analysedOutput = job.output;

    if (! waitForAudio (job.id, secondsNeededForAnalysis))
    {
        if (cancelled())
            return;
        r.kind = WorkerResult::Kind::Failed;
        r.error = "I didn't hear enough audio to analyse. Press play in your DAW and ask again.";
        postResult (std::move (r));
        return;
    }

    setStatus (true, job.id, "Analysing...");
    std::vector<float> left, right;
    copyClip (job.output, clipSeconds, left, right);

    dsp::AudioAnalyzer analyzer;
    analyzer.prepare (rollRate);
    analyzer.process (left.data(), right.data(), (int) left.size());
    r.analysis = analyzer.finish();
    r.secondsUsed = left.size() / rollRate;
    r.kind = r.analysis.valid ? WorkerResult::Kind::Analysis : WorkerResult::Kind::Failed;
    if (! r.analysis.valid)
        r.error = "The audio I heard was too quiet to analyse.";

    postResult (std::move (r));
}

} // namespace prizm
