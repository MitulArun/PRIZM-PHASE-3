/*
  ==============================================================================

    PRIZM V2 - mastering parametric equaliser.

    Low cut (24 dB/oct) -> low shelf -> 3 bells -> high shelf -> high cut
    (24 dB/oct). All parameters are smoothed and coefficients are recomputed
    at control rate (every 16 samples) while anything is moving; on/off
    switches crossfade so toggling a band or the whole module never clicks.

  ==============================================================================
*/

#pragma once

#include "Biquad.h"

namespace prizm::dsp
{

struct EQSettings
{
    bool enabled = true;

    bool hpfOn = false;
    float hpfFreq = 30.0f;

    float lsFreq = 100.0f, lsGain = 0.0f, lsQ = 0.707f;

    float pFreq[3] = { 250.0f, 1000.0f, 4000.0f };
    float pGain[3] = { 0.0f, 0.0f, 0.0f };
    float pQ[3]    = { 1.0f, 1.0f, 1.0f };

    float hsFreq = 10000.0f, hsGain = 0.0f, hsQ = 0.707f;

    bool lpfOn = false;
    float lpfFreq = 20000.0f;
};

class ParametricEQ
{
public:
    void prepare (double sampleRate);
    void setSettings (const EQSettings& s) noexcept;     // smoothed
    void reset (const EQSettings& s) noexcept;           // snap, clear state
    void process (float* left, float* right, int numSamples) noexcept;

    /** Magnitude response (dB) of the settings as they would sound with the
        module enabled. Used by the UI curve and by the assistant's EQ fitting. */
    static double responseDb (const EQSettings& s, double sampleRate, double freq) noexcept;

private:
    enum { numGainBands = 5 };   // low shelf, 3 bells, high shelf

    struct Band
    {
        LinearSmoother logFreq, gain, q;
        Biquad filter;
    };

    void updateCoefficients() noexcept;
    void snapSmoothers() noexcept;
    bool anySmoothing() const noexcept;

    double fs = 44100.0;
    EQSettings target;

    Band bands[numGainBands];
    LinearSmoother hpfLogFreq, lpfLogFreq;
    Biquad hpf[2], lpf[2];

    LinearSmoother moduleMix, hpfMix, lpfMix;
    bool filtersCleared = false;
};

} // namespace prizm::dsp
