/*
  ==============================================================================

    PRIZM V2 - 4x linear-phase oversampler (two polyphase half-band stages).

    Each stage is a Kaiser-windowed half-band FIR (every second tap is zero
    apart from the centre one, so the polyphase form only needs ~half the
    multiplies). Stage lengths are chosen, together with a 2-sample delay in
    the 4x domain, so that the round-trip latency is an exact integer number of
    base-rate samples. That lets the saturation module crossfade against a
    plain integer-delayed dry signal without any comb filtering.

  ==============================================================================
*/

#pragma once

#include "DspCommon.h"

namespace prizm::dsp
{

class HalfbandStage
{
public:
    /** length must be 4k+3. */
    void design (int length, double kaiserBeta);
    void prepare (int maxInputSamples);
    void reset() noexcept;

    /** n input samples -> 2n output samples. */
    void upsample (const float* in, float* out, int n) noexcept;

    /** 2n input samples -> n output samples. */
    void downsample (const float* in, float* out, int n) noexcept;

    /** Group delay of one pass (up or down), in samples of the higher rate. */
    int getCentre() const noexcept { return centre; }

private:
    std::vector<double> evenTaps;   // h[0], h[2], h[4] ... (non-zero taps)
    int centre = 0;

    // Circular histories (double-length buffers so a contiguous window is always available).
    std::vector<float> upHist, downEvenHist, downOddHist;
    int upPos = 0, downPos = 0, histLen = 0;
};

class Oversampler4x
{
public:
    void prepare (int maxBlockSize);
    void reset() noexcept;

    /** Upsample one channel; returns pointer to 4*n samples (valid until next call for that channel). */
    float* upsample (int channel, const float* in, int n) noexcept;

    /** Downsample the 4*n samples at 'data' (normally the pointer returned by upsample). */
    void downsample (int channel, const float* data, float* out, int n) noexcept;

    /** Exact round-trip latency in base-rate samples. */
    int getLatencySamples() const noexcept { return latency; }

private:
    struct Channel
    {
        HalfbandStage stage1, stage2;
        std::vector<float> buf2x, buf4x, tmp2x;
        float alignDelay[2] = { 0.0f, 0.0f };
    };

    Channel channels[2];
    int latency = 0;
};

} // namespace prizm::dsp
