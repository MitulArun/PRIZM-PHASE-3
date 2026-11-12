/*
  ==============================================================================

    PRIZM V2 - small radix-2 complex FFT.

    Generalises the hand-written FFT that V1 ran inside the editor's timer
    into a reusable class with precomputed twiddles and bit-reversal tables,
    shared by the UI spectrum display and the offline analyser.

  ==============================================================================
*/

#pragma once

#include "DspCommon.h"
#include <complex>

namespace prizm::dsp
{

class FFT
{
public:
    explicit FFT (int order = 12) { setOrder (order); }

    void setOrder (int order)
    {
        size = 1 << order;
        twiddles.resize ((size_t) size / 2);
        for (int i = 0; i < size / 2; ++i)
            twiddles[(size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * i / size));

        bitReverse.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b))
                    r |= 1 << (order - 1 - b);
            bitReverse[(size_t) i] = r;
        }
    }

    int getSize() const noexcept { return size; }

    /** In-place forward transform of 'size' complex values. */
    void perform (std::complex<float>* data) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = bitReverse[(size_t) i];
            if (i < j)
                std::swap (data[i], data[j]);
        }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, stride = size / len;
            for (int i = 0; i < size; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const auto u = data[i + j];
                    const auto v = data[i + j + half] * twiddles[(size_t) (j * stride)];
                    data[i + j] = u + v;
                    data[i + j + half] = u - v;
                }
            }
        }
    }

    /** Periodic Hann window of the given length. */
    static std::vector<float> hann (int length)
    {
        std::vector<float> w ((size_t) length);
        for (int i = 0; i < length; ++i)
            w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / length));
        return w;
    }

private:
    int size = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReverse;
};

} // namespace prizm::dsp
