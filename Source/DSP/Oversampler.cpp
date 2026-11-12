/*
  ==============================================================================

    PRIZM V2 - 4x linear-phase oversampler.

  ==============================================================================
*/

#include "Oversampler.h"

namespace prizm::dsp
{

namespace
{
    // Stage 1 (1x <-> 2x) needs a steep transition around the original Nyquist;
    // stage 2 (2x <-> 4x) only has to reject images far above the audio band.
    constexpr int stage1Length = 107;   // 4*26+3, ~80 dB, passband to ~0.45 fs
    constexpr int stage2Length = 23;    // 4*5+3
    constexpr double stage1Beta = 8.0;
    constexpr double stage2Beta = 8.9;
}

//==============================================================================
void HalfbandStage::design (int length, double beta)
{
    centre = (length - 1) / 2;
    const int numEven = (length + 1) / 2;
    evenTaps.assign ((size_t) numEven, 0.0);

    double sum = 0.0;
    for (int j = 0; j < numEven; ++j)
    {
        const int n = 2 * j;
        const double h = 0.5 * sinc ((n - centre) * 0.5) * kaiserWindow (n, length, beta);
        evenTaps[(size_t) j] = h;
        sum += h;
    }

    // Normalise so each polyphase branch has exactly unity DC gain.
    for (auto& t : evenTaps)
        t *= 0.5 / sum;

    histLen = numEven;
}

void HalfbandStage::prepare (int)
{
    upHist.assign ((size_t) histLen * 2, 0.0f);
    downEvenHist.assign ((size_t) histLen * 2, 0.0f);
    downOddHist.assign ((size_t) histLen * 2, 0.0f);
    upPos = downPos = 0;
}

void HalfbandStage::reset() noexcept
{
    std::fill (upHist.begin(), upHist.end(), 0.0f);
    std::fill (downEvenHist.begin(), downEvenHist.end(), 0.0f);
    std::fill (downOddHist.begin(), downOddHist.end(), 0.0f);
    upPos = downPos = 0;
}

void HalfbandStage::upsample (const float* in, float* out, int n) noexcept
{
    const int L = histLen;
    const int oddDelay = (centre - 1) / 2;
    const double* taps = evenTaps.data();

    for (int m = 0; m < n; ++m)
    {
        upPos = (upPos == 0 ? L : upPos) - 1;
        upHist[(size_t) upPos] = upHist[(size_t) (upPos + L)] = in[m];

        const float* w = upHist.data() + upPos;   // w[j] = x[m - j]
        double acc = 0.0;
        for (int j = 0; j < L; ++j)
            acc += taps[j] * w[j];

        out[2 * m]     = (float) (2.0 * acc);
        out[2 * m + 1] = w[oddDelay];
    }
}

void HalfbandStage::downsample (const float* in, float* out, int n) noexcept
{
    const int L = histLen;
    const int oddDelay = (centre + 1) / 2;
    const double* taps = evenTaps.data();

    for (int m = 0; m < n; ++m)
    {
        downPos = (downPos == 0 ? L : downPos) - 1;
        downEvenHist[(size_t) downPos] = downEvenHist[(size_t) (downPos + L)] = in[2 * m];
        downOddHist[(size_t) downPos]  = downOddHist[(size_t) (downPos + L)]  = in[2 * m + 1];

        const float* we = downEvenHist.data() + downPos;
        const float* wo = downOddHist.data() + downPos;

        double acc = 0.0;
        for (int j = 0; j < L; ++j)
            acc += taps[j] * we[j];

        out[m] = (float) (acc + 0.5 * wo[oddDelay]);
    }
}

//==============================================================================
void Oversampler4x::prepare (int maxBlockSize)
{
    for (auto& ch : channels)
    {
        ch.stage1.design (stage1Length, stage1Beta);
        ch.stage2.design (stage2Length, stage2Beta);
        ch.stage1.prepare (maxBlockSize);
        ch.stage2.prepare (maxBlockSize * 2);
        ch.buf2x.assign ((size_t) maxBlockSize * 2, 0.0f);
        ch.tmp2x.assign ((size_t) maxBlockSize * 2, 0.0f);
        ch.buf4x.assign ((size_t) maxBlockSize * 4, 0.0f);
    }

    // Round trip: stage 1 adds c1 base samples, stage 2 adds c2/2, the 2-sample
    // align delay at 4x adds 0.5 -> integer total.
    const int c1 = channels[0].stage1.getCentre();
    const int c2 = channels[0].stage2.getCentre();
    latency = c1 + (c2 + 1) / 2;

    reset();
}

void Oversampler4x::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.stage1.reset();
        ch.stage2.reset();
        ch.alignDelay[0] = ch.alignDelay[1] = 0.0f;
    }
}

float* Oversampler4x::upsample (int channel, const float* in, int n) noexcept
{
    auto& ch = channels[channel];
    ch.stage1.upsample (in, ch.buf2x.data(), n);
    ch.stage2.upsample (ch.buf2x.data(), ch.buf4x.data(), n * 2);
    return ch.buf4x.data();
}

void Oversampler4x::downsample (int channel, const float* data, float* out, int n) noexcept
{
    auto& ch = channels[channel];
    const int n4 = n * 4;

    // 2-sample delay at 4x (0.5 base-rate samples) so total latency is an integer.
    float* aligned = ch.buf4x.data();
    float d0 = ch.alignDelay[0], d1 = ch.alignDelay[1];
    for (int i = 0; i < n4; ++i)
    {
        const float x = data[i];
        aligned[i] = d0;
        d0 = d1;
        d1 = x;
    }
    ch.alignDelay[0] = d0;
    ch.alignDelay[1] = d1;

    ch.stage2.downsample (aligned, ch.tmp2x.data(), n * 2);
    ch.stage1.downsample (ch.tmp2x.data(), out, n);
}

} // namespace prizm::dsp
