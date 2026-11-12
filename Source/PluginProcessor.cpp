/*
  ==============================================================================

    PRIZM V2 - plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prizm;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Prizmv1AudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < NumParams; ++i)
    {
        const auto& info = paramInfo (i);
        const juce::ParameterID id { info.id, 1 };

        switch (info.kind)
        {
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, info.name, info.defaultValue >= 0.5f));
                break;

            case ParamKind::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, info.name,
                                                                          juce::StringArray::fromTokens (info.choices, "|", ""),
                                                                          (int) info.defaultValue));
                break;

            case ParamKind::Float:
            {
                juce::NormalisableRange<float> range (info.minValue, info.maxValue);
                if (info.skewCentre > 0.0f)
                    range.setSkewForCentre (info.skewCentre);

                const int index = i;
                auto attributes = juce::AudioParameterFloatAttributes()
                                      .withLabel (info.unit)
                                      .withStringFromValueFunction ([index] (float v, int) { return juce::String (formatParamValue (index, v)); });

                layout.add (std::make_unique<juce::AudioParameterFloat> (id, info.name, range, info.defaultValue, attributes));
                break;
            }
        }
    }

    return layout;
}

//==============================================================================
Prizmv1AudioProcessor::Prizmv1AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
     :
#endif
       apvts (*this, nullptr, "PRIZM", createParameterLayout())
{
    for (int i = 0; i < NumParams; ++i)
    {
        params[(size_t) i] = apvts.getParameter (paramInfo (i).id);
        rawParams[(size_t) i] = apvts.getRawParameterValue (paramInfo (i).id);
        jassert (params[(size_t) i] != nullptr && rawParams[(size_t) i] != nullptr);
    }

    for (auto* v : { &monoScratch, &inCopyL, &inCopyR, &tapL, &tapR, &monoIn, &monoOut })
        v->assign ((size_t) scratchSize, 0.0f);

    history = std::make_unique<HistoryManager> (*this, getParameters());
    chat = std::make_unique<ChatSession> (*this);
}

Prizmv1AudioProcessor::~Prizmv1AudioProcessor()
{
    chat.reset();
    history.reset();
}

//==============================================================================
const juce::String Prizmv1AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Prizmv1AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Prizmv1AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Prizmv1AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double Prizmv1AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Prizmv1AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Prizmv1AudioProcessor::getCurrentProgram()
{
    return 0;
}

void Prizmv1AudioProcessor::setCurrentProgram (int)
{
}

const juce::String Prizmv1AudioProcessor::getProgramName (int)
{
    return {};
}

void Prizmv1AudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void Prizmv1AudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    chain.prepare (sampleRate);
    chain.reset (dsp::makeChainSettings (getParamState()));

    const int latency = chain.getLatencySamples();
    latencySamples = latency;
    setLatencySamples (latency);

    for (auto& d : bypassDelay)
    {
        d.prepare (latency);
        d.setDelay (latency);
    }

    inLoudness.prepare (sampleRate);
    outLoudness.prepare (sampleRate);
    for (auto& tp : truePeak)
        tp.reset();

    inRmsState[0] = inRmsState[1] = outRmsState[0] = outRmsState[1] = 0.0f;
    corrLR = corrLL = corrRR = 0.0;
    truePeakMax = 0.0f;

    worker.setSampleRate (sampleRate);
}

void Prizmv1AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Prizmv1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Mono or stereo, input matching output (unchanged from V1).
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

//==============================================================================
void Prizmv1AudioProcessor::measureInput (const float* l, const float* r, int n) noexcept
{
    const double fs = currentSampleRate.load();
    const float a = (float) std::exp (-n / (0.3 * fs));   // ~300 ms RMS ballistics
    const float* ch[2] = { l, r };

    for (int c = 0; c < 2; ++c)
    {
        float peak = 0.0f;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            peak = std::max (peak, std::abs (ch[c][i]));
            sum += (double) ch[c][i] * ch[c][i];
        }

        snapshot.inPeak[c] = std::max (peak, snapshot.inPeak[c]);
        inRmsState[c] = a * inRmsState[c] + (1.0f - a) * (float) (sum / n);
        snapshot.inRms[c] = std::sqrt (inRmsState[c]);
    }

    inLoudness.process (l, r, n);
}

void Prizmv1AudioProcessor::measureOutput (const float* l, const float* r, int n) noexcept
{
    const double fs = currentSampleRate.load();
    const float a = (float) std::exp (-n / (0.3 * fs));
    const float* ch[2] = { l, r };
    float tpBlock = 0.0f;

    for (int c = 0; c < 2; ++c)
    {
        float peak = 0.0f;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = ch[c][i];
            peak = std::max (peak, std::abs (x));
            sum += (double) x * x;
            tpBlock = std::max (tpBlock, truePeak[c].process (x));
        }

        snapshot.outPeak[c] = std::max (peak, snapshot.outPeak[c]);
        outRmsState[c] = a * outRmsState[c] + (1.0f - a) * (float) (sum / n);
        snapshot.outRms[c] = std::sqrt (outRmsState[c]);
    }

    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        lr += (double) l[i] * r[i];
        ll += (double) l[i] * l[i];
        rr += (double) r[i] * r[i];
    }

    corrLR = a * corrLR + (1.0 - a) * lr / n;
    corrLL = a * corrLL + (1.0 - a) * ll / n;
    corrRR = a * corrRR + (1.0 - a) * rr / n;

    snapshot.outTruePeakBlock = std::max (snapshot.outTruePeakBlock, tpBlock);
    truePeakMax = std::max (truePeakMax, tpBlock);
    outLoudness.process (l, r, n);
}

void Prizmv1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // V1 forced silence in Standalone mode. V2 removes that: the JUCE standalone
    // wrapper already mutes the audio input by default to avoid feedback, and
    // the user can unmute it in the audio settings to process a live source.

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || totalNumInputChannels == 0)
        return;

    if (meterResetRequested.exchange (false))
    {
        inLoudness.resetIntegrated();
        outLoudness.resetIntegrated();
        truePeakMax = 0.0f;
    }

    // Parameters -> DSP (smoothed inside the chain).
    ParamState ps;
    for (int i = 0; i < NumParams; ++i)
        ps[(size_t) i] = rawParams[(size_t) i]->load (std::memory_order_relaxed);
    chain.setSettings (dsp::makeChainSettings (ps));

    dsp::MonitorSettings monitor;
    monitor.listenToOriginal = compareOriginal.load();
    monitor.originalGainDb = levelMatch.load() ? matchDb : 0.0f;
    monitor.monoCheck = monoCheck.load();
    chain.setMonitor (monitor);

    const bool stereo = totalNumInputChannels > 1 && buffer.getNumChannels() > 1;
    float* left = buffer.getWritePointer (0);
    float* right = stereo ? buffer.getWritePointer (1) : nullptr;

    // Peaks are held for 50 ms so the UI (30 Hz) can never miss one between reads.
    peakHoldRemaining -= numSamples;
    if (peakHoldRemaining <= 0)
    {
        snapshot.inPeak[0] = snapshot.inPeak[1] = snapshot.outPeak[0] = snapshot.outPeak[1] = 0.0f;
        snapshot.outTruePeakBlock = 0.0f;
        snapshot.compGrDb = snapshot.limGrDb = 0.0f;
        peakHoldRemaining = (int) (currentSampleRate.load() * 0.05);
    }

    float compGr = 0.0f, limGr = 0.0f;

    for (int start = 0; start < numSamples; start += scratchSize)
    {
        const int n = std::min (scratchSize, numSamples - start);
        float* l = left + start;
        float* r = stereo ? right + start : monoScratch.data();

        if (! stereo)
            std::copy (l, l + n, r);   // mono: process as dual-mono, write back channel 0

        std::copy (l, l + n, inCopyL.data());
        std::copy (r, r + n, inCopyR.data());
        measureInput (l, r, n);

        chain.process (l, r, n, tapL.data(), tapR.data());
        compGr = std::max (compGr, chain.getCompressorGainReductionDb());
        limGr = std::max (limGr, chain.getLimiterGainReductionDb());

        measureOutput (tapL.data(), tapR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            monoIn[(size_t) i] = 0.5f * (inCopyL[(size_t) i] + inCopyR[(size_t) i]);
            monoOut[(size_t) i] = 0.5f * (tapL[(size_t) i] + tapR[(size_t) i]);
        }

        const float* spectrum[2] = { monoIn.data(), monoOut.data() };
        spectrumFifo.push (spectrum, n);
        const float* scope[2] = { tapL.data(), tapR.data() };
        scopeFifo.push (scope, n);
        worker.pushAudio (inCopyL.data(), inCopyR.data(), tapL.data(), tapR.data(), n);
    }

    // Level-matched A/B: bring the original up/down to the processed loudness.
    const float inS = inLoudness.getShortTerm(), outS = outLoudness.getShortTerm();
    if (inS > -70.0f && outS > -70.0f)
        matchDb = juce::jlimit (-24.0f, 24.0f, outS - inS);

    const bool active = snapshot.inRms[0] > 3.0e-4f || snapshot.inRms[1] > 3.0e-4f;   // ~ -70 dBFS
    silentBlocks = active ? 0 : silentBlocks + 1;

    snapshot.inShortTerm = inS;
    snapshot.inMomentary = inLoudness.getMomentary();
    snapshot.inIntegrated = inLoudness.getIntegrated();
    snapshot.outMomentary = outLoudness.getMomentary();
    snapshot.outShortTerm = outS;
    snapshot.outIntegrated = outLoudness.getIntegrated();
    snapshot.outRange = outLoudness.getLoudnessRange();
    snapshot.outTruePeakMaxDb = dsp::gainToDb (truePeakMax, -144.0f);
    snapshot.compGrDb = std::max (snapshot.compGrDb, compGr);
    snapshot.limGrDb = std::max (snapshot.limGrDb, limGr);
    snapshot.correlation = (corrLL * corrRR > 1.0e-12) ? (float) (corrLR / std::sqrt (corrLL * corrRR)) : 1.0f;
    snapshot.originalMatchDb = matchDb;
    snapshot.audioActive = silentBlocks * numSamples < (int) (currentSampleRate.load() * 0.5);
    meters.store (snapshot);
}

void Prizmv1AudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass: pass audio through delayed by the reported latency so the
    // host's delay compensation keeps everything aligned.
    const int numSamples = buffer.getNumSamples();
    for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = bypassDelay[ch].process (d[i]);
    }

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
ParamState Prizmv1AudioProcessor::getParamState() const
{
    ParamState s;
    for (int i = 0; i < NumParams; ++i)
        s[(size_t) i] = rawParams[(size_t) i]->load();
    return s;
}

void Prizmv1AudioProcessor::setParamState (const ParamState& state)
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (int i = 0; i < NumParams; ++i)
    {
        auto* p = params[(size_t) i];
        const float target = p->convertTo0to1 (clampParam (i, state[(size_t) i]));
        if (std::abs (p->getValue() - target) < 1.0e-6f)
            continue;

        p->beginChangeGesture();
        p->setValueNotifyingHost (target);
        p->endChangeGesture();
    }
}

//==============================================================================
Prizmv1AudioProcessor::ReferenceInfo Prizmv1AudioProcessor::getReference() const
{
    const juce::ScopedLock sl (referenceLock);
    return reference;
}

void Prizmv1AudioProcessor::setReference (const ReferenceInfo& info)
{
    {
        const juce::ScopedLock sl (referenceLock);
        reference = info;
    }
    ++referenceVersion;
}

void Prizmv1AudioProcessor::clearReference()
{
    setReference ({});
}

//==============================================================================
bool Prizmv1AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Prizmv1AudioProcessor::createEditor()
{
    return new Prizmv1AudioProcessorEditor (*this);
}

//==============================================================================
void Prizmv1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;

    auto* extra = xml->createNewChildElement ("PRIZM_V2");
    extra->setAttribute ("targetLufs", (double) targetLufs.load());
    extra->setAttribute ("ceiling", (double) ceilingDb.load());

    const auto ref = getReference();
    if (ref.valid)
    {
        extra->setAttribute ("refName", ref.name);
        extra->setAttribute ("refPath", ref.path);
        extra->setAttribute ("refAnalysis", juce::String (ref.analysis.serialise()));
    }

    copyXmlToBinary (*xml, destData);
}

void Prizmv1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    if (auto* extra = xml->getChildByName ("PRIZM_V2"))
    {
        targetLufs = (float) extra->getDoubleAttribute ("targetLufs", -14.0);
        ceilingDb = (float) extra->getDoubleAttribute ("ceiling", -1.0);

        ReferenceInfo ref;
        if (extra->hasAttribute ("refAnalysis")
            && dsp::AnalysisResult::deserialise (extra->getStringAttribute ("refAnalysis").toStdString(), ref.analysis))
        {
            ref.name = extra->getStringAttribute ("refName");
            ref.path = extra->getStringAttribute ("refPath");
            ref.valid = ref.analysis.valid;
        }
        setReference (ref);

        xml->removeChildElement (extra, true);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    sessionStartPending = true;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Prizmv1AudioProcessor();
}
