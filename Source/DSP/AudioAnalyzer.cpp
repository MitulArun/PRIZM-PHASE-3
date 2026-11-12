/*
  ==============================================================================

    PRIZM V2 - offline audio analysis.

  ==============================================================================
*/

#include "AudioAnalyzer.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace prizm::dsp
{

namespace
{
    const char* const bandNames[AnalysisResult::numBands] = { "Sub", "Bass", "Low-mids", "Mids", "Presence", "Air" };
    const float bandEdges[AnalysisResult::numBands + 1] = { 20.0f, 60.0f, 200.0f, 600.0f, 2500.0f, 6000.0f, 20000.0f };

    constexpr float lowEndSplitHz = 150.0f;
    constexpr double silentBlockMeanSquare = 1.0e-7;   // -70 dBFS
}

const char* AnalysisResult::bandName (int band) noexcept    { return bandNames[clampValue (band, 0, numBands - 1)]; }
float AnalysisResult::bandLowHz (int band) noexcept         { return bandEdges[clampValue (band, 0, numBands - 1)]; }
float AnalysisResult::bandHighHz (int band) noexcept        { return bandEdges[clampValue (band, 0, numBands - 1) + 1]; }

float AnalysisResult::pointFrequency (int index) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) index / (float) (numPoints - 1));
}

std::array<float, AnalysisResult::numPoints> AnalysisResult::normalisedMid() const noexcept
{
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < numPoints; ++i)
    {
        if (freqs[(size_t) i] >= 100.0f && freqs[(size_t) i] <= 8000.0f)
        {
            sum += midDb[(size_t) i];
            ++n;
        }
    }

    const float mean = n > 0 ? (float) (sum / n) : 0.0f;
    auto out = midDb;
    for (auto& v : out)
        v -= mean;
    return out;
}

//==============================================================================
std::string AnalysisResult::serialise() const
{
    std::ostringstream s;
    s.precision (6);
    s << "v=1;valid=" << (valid ? 1 : 0) << ";sr=" << sampleRate << ";sec=" << secondsAnalysed
      << ";il=" << integratedLufs << ";lra=" << loudnessRange << ";mst=" << maxShortTermLufs
      << ";sp=" << samplePeakDb << ";tp=" << truePeakDb << ";rms=" << rmsDb << ";crest=" << crestDb
      << ";plr=" << plrDb << ";p50=" << blockRmsP50 << ";p90=" << blockRmsP90
      << ";corr=" << correlation << ";sm=" << sideToMidDb << ";lsm=" << lowSideToMidDb
      << ";bal=" << balanceDb << ";tilt=" << tiltDbPerOctave;

    auto writeArray = [&s] (const char* key, const auto& arr)
    {
        s << ";" << key << "=";
        for (size_t i = 0; i < arr.size(); ++i)
            s << (i ? "," : "") << arr[i];
    };

    writeArray ("f", freqs);
    writeArray ("mid", midDb);
    writeArray ("side", sideDb);
    writeArray ("band", bandDb);
    return s.str();
}

bool AnalysisResult::deserialise (const std::string& text, AnalysisResult& out)
{
    AnalysisResult r;
    std::istringstream in (text);
    std::string item;
    bool sawVersion = false;

    auto readArray = [] (const std::string& value, auto& arr)
    {
        std::istringstream vs (value);
        std::string tok;
        size_t i = 0;
        while (std::getline (vs, tok, ',') && i < arr.size())
            arr[i++] = std::strtof (tok.c_str(), nullptr);
        return i == arr.size();
    };

    while (std::getline (in, item, ';'))
    {
        const auto eq = item.find ('=');
        if (eq == std::string::npos)
            continue;

        const std::string key = item.substr (0, eq), value = item.substr (eq + 1);
        const float f = std::strtof (value.c_str(), nullptr);

        if (key == "v")          sawVersion = true;
        else if (key == "valid") r.valid = f > 0.5f;
        else if (key == "sr")    r.sampleRate = f;
        else if (key == "sec")   r.secondsAnalysed = f;
        else if (key == "il")    r.integratedLufs = f;
        else if (key == "lra")   r.loudnessRange = f;
        else if (key == "mst")   r.maxShortTermLufs = f;
        else if (key == "sp")    r.samplePeakDb = f;
        else if (key == "tp")    r.truePeakDb = f;
        else if (key == "rms")   r.rmsDb = f;
        else if (key == "crest") r.crestDb = f;
        else if (key == "plr")   r.plrDb = f;
        else if (key == "p50")   r.blockRmsP50 = f;
        else if (key == "p90")   r.blockRmsP90 = f;
        else if (key == "corr")  r.correlation = f;
        else if (key == "sm")    r.sideToMidDb = f;
        else if (key == "lsm")   r.lowSideToMidDb = f;
        else if (key == "bal")   r.balanceDb = f;
        else if (key == "tilt")  r.tiltDbPerOctave = f;
        else if (key == "f")     { if (! readArray (value, r.freqs)) return false; }
        else if (key == "mid")   { if (! readArray (value, r.midDb)) return false; }
        else if (key == "side")  { if (! readArray (value, r.sideDb)) return false; }
        else if (key == "band")  { if (! readArray (value, r.bandDb)) return false; }
    }

    if (! sawVersion)
        return false;

    out = r;
    return true;
}

//==============================================================================
void AudioAnalyzer::prepare (double sampleRate)
{
    fs = sampleRate;
    samplesProcessed = 0;

    loudness.prepare (fs);
    tpL.reset();
    tpR.reset();
    samplePeak = truePeak = 0.0f;

    blockLength = std::max (1, (int) std::lround (fs * 0.1));
    blockCount = 0;
    blockSumL = blockSumR = blockSumLR = 0.0;
    audibleSumSq = sumL2 = sumR2 = sumLR = 0.0;
    audibleSamples = 0;
    blockRmsHist.fill (0);

    const int order = fs <= 50000.0 ? 12 : (fs <= 100000.0 ? 13 : 14);
    fft.setOrder (order);
    fftSize = fft.getSize();
    hop = fftSize / 2;
    window = FFT::hann (fftSize);
    fifoMid.assign ((size_t) fftSize, 0.0f);
    fifoSide.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize, {});
    fifoPos = 0;

    const double binHz = fs / fftSize;
    const int maxBin = fftSize / 2 - 1;
    for (int k = 0; k < AnalysisResult::numPoints; ++k)
    {
        const double f = AnalysisResult::pointFrequency (k);
        const double lo = f / std::pow (2.0, 1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        int bLo = (int) std::ceil (lo / binHz), bHi = (int) std::floor (hi / binHz);

        if (bHi < bLo)   // narrower than one bin at low frequencies: use the nearest bin
            bLo = bHi = (int) std::lround (f / binHz);

        pointLo[(size_t) k] = clampValue (bLo, 1, maxBin);
        pointHi[(size_t) k] = clampValue (bHi, 1, maxBin);
    }

    buckets.assign (numLevelBuckets, Bucket {});
}

void AudioAnalyzer::process (const float* left, const float* right, int numSamples) noexcept
{
    loudness.process (left, right, numSamples);
    samplesProcessed += numSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        samplePeak = std::max (samplePeak, std::max (std::abs (l), std::abs (r)));
        truePeak = std::max (truePeak, std::max (tpL.process (l), tpR.process (r)));

        blockSumL += (double) l * l;
        blockSumR += (double) r * r;
        blockSumLR += (double) l * r;

        if (++blockCount >= blockLength)
        {
            const double meanSquare = (blockSumL + blockSumR) / (2.0 * blockCount);

            if (meanSquare > silentBlockMeanSquare)
            {
                audibleSumSq += blockSumL + blockSumR;
                sumL2 += blockSumL;
                sumR2 += blockSumR;
                sumLR += blockSumLR;
                audibleSamples += blockCount;

                const int bin = clampValue ((int) std::floor ((powerToDb (meanSquare) + 110.0) * 2.0), 0, (int) blockRmsHist.size() - 1);
                ++blockRmsHist[(size_t) bin];
            }

            blockSumL = blockSumR = blockSumLR = 0.0;
            blockCount = 0;
        }

        fifoMid[(size_t) fifoPos] = 0.5f * (l + r);
        fifoSide[(size_t) fifoPos] = 0.5f * (l - r);

        if (++fifoPos >= fftSize)
        {
            analyseFrame();
            std::copy (fifoMid.begin() + hop, fifoMid.end(), fifoMid.begin());
            std::copy (fifoSide.begin() + hop, fifoSide.end(), fifoSide.begin());
            fifoPos = fftSize - hop;
        }
    }
}

void AudioAnalyzer::analyseFrame()
{
    double frameEnergy = 0.0;
    double windowPower = 0.0;

    for (int n = 0; n < fftSize; ++n)
    {
        const float m = fifoMid[(size_t) n], s = fifoSide[(size_t) n], w = window[(size_t) n];
        frameEnergy += (double) m * m + (double) s * s;
        windowPower += (double) w * w;
        fftData[(size_t) n] = { m * w, s * w };
    }

    const double levelDb = powerToDb (frameEnergy / fftSize);
    if (levelDb < -100.0)
        return;

    fft.perform (fftData.data());

    const double binHz = fs / fftSize;
    const double scale = 1.0 / (windowPower * binHz);   // -> power density per Hz
    const int half = fftSize / 2;

    auto& bucket = buckets[(size_t) clampValue ((int) std::floor (levelDb + 110.0), 0, numLevelBuckets - 1)];
    ++bucket.frames;

    // Separate the two real spectra packed into one complex FFT.
    auto& pm = powerMid;
    auto& ps = powerSide;
    pm.resize ((size_t) half);
    ps.resize ((size_t) half);

    for (int k = 1; k < half; ++k)
    {
        const auto X = fftData[(size_t) k];
        const auto Y = std::conj (fftData[(size_t) (fftSize - k)]);
        const auto M = (X + Y) * 0.5f;
        const auto D = (X - Y) * 0.5f;
        const std::complex<float> S (D.imag(), -D.real());

        pm[(size_t) k] = std::norm (M) * scale;
        ps[(size_t) k] = std::norm (S) * scale;

        const double f = k * binHz;
        if (f < 20.0 || f > 20000.0)
            continue;

        const double energy = (pm[(size_t) k] + ps[(size_t) k]) * binHz;
        bucket.total += energy;

        for (int b = 0; b < AnalysisResult::numBands; ++b)
        {
            if (f >= bandEdges[b] && f < bandEdges[b + 1])
            {
                bucket.band[(size_t) b] += energy;
                break;
            }
        }

        if (f < lowEndSplitHz)
        {
            bucket.lowMid += pm[(size_t) k] * binHz;
            bucket.lowSide += ps[(size_t) k] * binHz;
        }
        else if (f < 16000.0)
        {
            bucket.highMid += pm[(size_t) k] * binHz;
            bucket.highSide += ps[(size_t) k] * binHz;
        }
    }

    for (int p = 0; p < AnalysisResult::numPoints; ++p)
    {
        const int lo = pointLo[(size_t) p], hi = pointHi[(size_t) p];
        double sm = 0.0, ss = 0.0;
        for (int k = lo; k <= hi; ++k)
        {
            sm += pm[(size_t) k];
            ss += ps[(size_t) k];
        }

        const double count = (double) (hi - lo + 1);
        bucket.mid[(size_t) p] += sm / count;
        bucket.side[(size_t) p] += ss / count;
    }
}

AnalysisResult AudioAnalyzer::finish()
{
    // Short material: analyse the partial frame (zero-padded) so we still get a spectrum.
    int totalFrames = 0;
    for (auto& b : buckets)
        totalFrames += b.frames;

    if (totalFrames == 0 && fifoPos > 0)
    {
        std::fill (fifoMid.begin() + fifoPos, fifoMid.end(), 0.0f);
        std::fill (fifoSide.begin() + fifoPos, fifoSide.end(), 0.0f);
        analyseFrame();
        for (auto& b : buckets)
            totalFrames += b.frames;
    }

    AnalysisResult r;
    r.sampleRate = fs;
    r.secondsAnalysed = (double) audibleSamples / fs;

    for (int k = 0; k < AnalysisResult::numPoints; ++k)
        r.freqs[(size_t) k] = AnalysisResult::pointFrequency (k);

    r.integratedLufs = loudness.getIntegrated();
    r.loudnessRange = loudness.getLoudnessRange();
    r.maxShortTermLufs = loudness.getMaxShortTerm();
    r.samplePeakDb = gainToDb (samplePeak, -144.0f);
    r.truePeakDb = gainToDb (truePeak, -144.0f);

    if (audibleSamples > 0)
    {
        r.rmsDb = (float) powerToDb (audibleSumSq / (2.0 * (double) audibleSamples), -144.0);
        r.crestDb = r.samplePeakDb - r.rmsDb;
        r.plrDb = r.integratedLufs > LoudnessMeter::silence ? r.truePeakDb - r.integratedLufs : 0.0f;
        r.correlation = (sumL2 > 0.0 && sumR2 > 0.0) ? (float) (sumLR / std::sqrt (sumL2 * sumR2)) : 1.0f;
        r.balanceDb = (sumL2 > 0.0 && sumR2 > 0.0) ? (float) (10.0 * std::log10 (sumL2 / sumR2)) : 0.0f;

        int blocks = 0;
        for (int c : blockRmsHist)
            blocks += c;

        auto percentile = [&] (double p)
        {
            const int rank = std::max (1, (int) std::ceil (p * blocks));
            int seen = 0;
            for (size_t b = 0; b < blockRmsHist.size(); ++b)
            {
                seen += blockRmsHist[b];
                if (seen >= rank)
                    return (float) (b * 0.5 - 110.0 + 0.25);
            }
            return -144.0f;
        };

        r.blockRmsP50 = percentile (0.5);
        r.blockRmsP90 = percentile (0.9);
    }

    if (totalFrames > 0)
    {
        // Level gate: ignore quiet frames (fades, intros, gaps) so the spectrum
        // reflects the body of the track.
        int seen = 0, p90Bucket = 0;
        for (int b = numLevelBuckets - 1; b >= 0; --b)
        {
            seen += buckets[(size_t) b].frames;
            if (seen * 10 >= totalFrames)
            {
                p90Bucket = b;
                break;
            }
        }

        int gateBucket = p90Bucket - 20;                 // 20 dB below the loud parts...
        if (p90Bucket >= 50)
            gateBucket = std::max (gateBucket, 50);      // ...and never below -60 dBFS
        gateBucket = std::max (0, gateBucket);

        Bucket sum;
        for (int b = gateBucket; b < numLevelBuckets; ++b)
        {
            const auto& src = buckets[(size_t) b];
            sum.frames += src.frames;
            for (size_t k = 0; k < sum.mid.size(); ++k) { sum.mid[k] += src.mid[k]; sum.side[k] += src.side[k]; }
            for (size_t k = 0; k < sum.band.size(); ++k) sum.band[k] += src.band[k];
            sum.lowMid += src.lowMid; sum.lowSide += src.lowSide;
            sum.highMid += src.highMid; sum.highSide += src.highSide;
            sum.total += src.total;
        }

        if (sum.frames > 0)
        {
            for (int k = 0; k < AnalysisResult::numPoints; ++k)
            {
                r.midDb[(size_t) k] = (float) powerToDb (sum.mid[(size_t) k] / sum.frames, -200.0);
                r.sideDb[(size_t) k] = (float) powerToDb (sum.side[(size_t) k] / sum.frames, -200.0);
            }

            for (int b = 0; b < AnalysisResult::numBands; ++b)
                r.bandDb[(size_t) b] = sum.total > 0.0 ? (float) powerToDb (sum.band[(size_t) b] / sum.total, -100.0) : -100.0f;

            r.lowSideToMidDb = sum.lowMid > 0.0 ? (float) powerToDb (sum.lowSide / sum.lowMid, -60.0) : -60.0f;
            r.sideToMidDb = sum.highMid > 0.0 ? (float) powerToDb (sum.highSide / sum.highMid, -60.0) : -60.0f;

            // Spectral tilt: least-squares slope of the mid spectrum vs log2 (f).
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int n = 0;
            for (int k = 0; k < AnalysisResult::numPoints; ++k)
            {
                const double f = r.freqs[(size_t) k];
                if (f < 100.0 || f > 10000.0)
                    continue;
                const double x = std::log2 (f), y = r.midDb[(size_t) k];
                sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
            }

            if (n > 2)
                r.tiltDbPerOctave = (float) ((n * sxy - sx * sy) / (n * sxx - sx * sx));
        }
    }

    r.valid = audibleSamples >= (long long) fs && totalFrames > 0;
    return r;
}

} // namespace prizm::dsp
