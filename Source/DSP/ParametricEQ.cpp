/*
  ==============================================================================

    PRIZM V2 - mastering parametric equaliser.

  ==============================================================================
*/

#include "ParametricEQ.h"

namespace prizm::dsp
{

namespace
{
    constexpr int controlBlock = 16;
    constexpr double paramRampSeconds = 0.05;
    constexpr double switchRampSeconds = 0.02;

    float log2f (float hz) noexcept { return std::log2 (std::max (1.0f, hz)); }
    double fromLog2 (float v) noexcept { return std::pow (2.0, (double) v); }
}

void ParametricEQ::prepare (double sampleRate)
{
    fs = sampleRate;

    for (auto& b : bands)
    {
        b.logFreq.prepare (fs, paramRampSeconds);
        b.gain.prepare (fs, paramRampSeconds);
        b.q.prepare (fs, paramRampSeconds);
    }

    hpfLogFreq.prepare (fs, paramRampSeconds);
    lpfLogFreq.prepare (fs, paramRampSeconds);
    moduleMix.prepare (fs, switchRampSeconds);
    hpfMix.prepare (fs, switchRampSeconds);
    lpfMix.prepare (fs, switchRampSeconds);

    reset (target);
}

void ParametricEQ::setSettings (const EQSettings& s) noexcept
{
    target = s;

    bands[0].logFreq.setTarget (log2f (s.lsFreq));
    bands[0].gain.setTarget (s.lsGain);
    bands[0].q.setTarget (s.lsQ);

    for (int i = 0; i < 3; ++i)
    {
        bands[i + 1].logFreq.setTarget (log2f (s.pFreq[i]));
        bands[i + 1].gain.setTarget (s.pGain[i]);
        bands[i + 1].q.setTarget (s.pQ[i]);
    }

    bands[4].logFreq.setTarget (log2f (s.hsFreq));
    bands[4].gain.setTarget (s.hsGain);
    bands[4].q.setTarget (s.hsQ);

    hpfLogFreq.setTarget (log2f (s.hpfFreq));
    lpfLogFreq.setTarget (log2f (s.lpfFreq));

    moduleMix.setTarget (s.enabled ? 1.0f : 0.0f);
    hpfMix.setTarget (s.hpfOn ? 1.0f : 0.0f);
    lpfMix.setTarget (s.lpfOn ? 1.0f : 0.0f);
}

void ParametricEQ::reset (const EQSettings& s) noexcept
{
    setSettings (s);
    snapSmoothers();

    for (auto& b : bands)
        b.filter.reset();

    for (int i = 0; i < 2; ++i)
    {
        hpf[i].reset();
        lpf[i].reset();
    }

    updateCoefficients();
}

void ParametricEQ::snapSmoothers() noexcept
{
    for (auto& b : bands)
    {
        b.logFreq.reset (b.logFreq.getTarget());
        b.gain.reset (b.gain.getTarget());
        b.q.reset (b.q.getTarget());
    }

    hpfLogFreq.reset (hpfLogFreq.getTarget());
    lpfLogFreq.reset (lpfLogFreq.getTarget());
    moduleMix.reset (moduleMix.getTarget());
    hpfMix.reset (hpfMix.getTarget());
    lpfMix.reset (lpfMix.getTarget());
}

bool ParametricEQ::anySmoothing() const noexcept
{
    for (auto& b : bands)
        if (b.logFreq.isSmoothing() || b.gain.isSmoothing() || b.q.isSmoothing())
            return true;

    return hpfLogFreq.isSmoothing() || lpfLogFreq.isSmoothing();
}

void ParametricEQ::updateCoefficients() noexcept
{
    bands[0].filter.setCoeffs (BiquadCoeffs::lowShelf (fs, fromLog2 (bands[0].logFreq.getCurrent()),
                                                       bands[0].q.getCurrent(), bands[0].gain.getCurrent()));

    for (int i = 1; i <= 3; ++i)
        bands[i].filter.setCoeffs (BiquadCoeffs::peak (fs, fromLog2 (bands[i].logFreq.getCurrent()),
                                                       bands[i].q.getCurrent(), bands[i].gain.getCurrent()));

    bands[4].filter.setCoeffs (BiquadCoeffs::highShelf (fs, fromLog2 (bands[4].logFreq.getCurrent()),
                                                        bands[4].q.getCurrent(), bands[4].gain.getCurrent()));

    const double hpFreq = fromLog2 (hpfLogFreq.getCurrent());
    hpf[0].setCoeffs (BiquadCoeffs::highPass (fs, hpFreq, butterworth4Q1));
    hpf[1].setCoeffs (BiquadCoeffs::highPass (fs, hpFreq, butterworth4Q2));

    const double lpFreq = std::min (fromLog2 (lpfLogFreq.getCurrent()), fs * 0.45);
    lpf[0].setCoeffs (BiquadCoeffs::lowPass (fs, lpFreq, butterworth4Q1));
    lpf[1].setCoeffs (BiquadCoeffs::lowPass (fs, lpFreq, butterworth4Q2));
}

void ParametricEQ::process (float* left, float* right, int numSamples) noexcept
{
    float* channels[2] = { left, right };

    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int len = std::min (controlBlock, numSamples - start);

        // Fully bypassed and settled: pass audio through untouched and keep the
        // filters cleared so re-enabling starts from a clean state.
        if (! moduleMix.isSmoothing() && moduleMix.getCurrent() <= 0.0f)
        {
            if (! filtersCleared || anySmoothing())
            {
                for (auto& b : bands)
                    b.filter.reset();
                for (int i = 0; i < 2; ++i) { hpf[i].reset(); lpf[i].reset(); }
                snapSmoothers();
                updateCoefficients();
                filtersCleared = true;
            }

            continue;
        }

        filtersCleared = false;

        if (anySmoothing())
        {
            for (auto& b : bands)
            {
                b.logFreq.skip (len);
                b.gain.skip (len);
                b.q.skip (len);
            }

            hpfLogFreq.skip (len);
            lpfLogFreq.skip (len);
            updateCoefficients();
        }

        const bool hpfActive = hpfMix.isSmoothing() || hpfMix.getCurrent() > 0.0f;
        const bool lpfActive = lpfMix.isSmoothing() || lpfMix.getCurrent() > 0.0f;

        if (! hpfActive) { hpf[0].reset(); hpf[1].reset(); }
        if (! lpfActive) { lpf[0].reset(); lpf[1].reset(); }

        for (int i = start; i < start + len; ++i)
        {
            const float modMix = moduleMix.next();
            const float hMix = hpfMix.next();
            const float lMix = lpfMix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                const float dry = channels[ch][i];
                float x = dry;

                if (hpfActive)
                {
                    const float filtered = hpf[1].process (hpf[0].process (x, ch), ch);
                    x += (filtered - x) * hMix;
                }

                for (auto& b : bands)
                    x = b.filter.process (x, ch);

                if (lpfActive)
                {
                    const float filtered = lpf[1].process (lpf[0].process (x, ch), ch);
                    x += (filtered - x) * lMix;
                }

                channels[ch][i] = dry + (x - dry) * modMix;
            }
        }
    }
}

double ParametricEQ::responseDb (const EQSettings& s, double sampleRate, double freq) noexcept
{
    std::complex<double> h (1.0, 0.0);

    if (s.hpfOn)
    {
        h *= BiquadCoeffs::highPass (sampleRate, s.hpfFreq, butterworth4Q1).response (sampleRate, freq);
        h *= BiquadCoeffs::highPass (sampleRate, s.hpfFreq, butterworth4Q2).response (sampleRate, freq);
    }

    h *= BiquadCoeffs::lowShelf (sampleRate, s.lsFreq, s.lsQ, s.lsGain).response (sampleRate, freq);

    for (int i = 0; i < 3; ++i)
        h *= BiquadCoeffs::peak (sampleRate, s.pFreq[i], s.pQ[i], s.pGain[i]).response (sampleRate, freq);

    h *= BiquadCoeffs::highShelf (sampleRate, s.hsFreq, s.hsQ, s.hsGain).response (sampleRate, freq);

    if (s.lpfOn)
    {
        const double lp = std::min ((double) s.lpfFreq, sampleRate * 0.45);
        h *= BiquadCoeffs::lowPass (sampleRate, lp, butterworth4Q1).response (sampleRate, freq);
        h *= BiquadCoeffs::lowPass (sampleRate, lp, butterworth4Q2).response (sampleRate, freq);
    }

    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}

} // namespace prizm::dsp
