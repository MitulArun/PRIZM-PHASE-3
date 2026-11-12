/*
  ==============================================================================

    PRIZM V2 - lock-free single-producer / single-consumer audio FIFO.

    The audio thread pushes; one other thread (UI timer or analysis worker)
    pops. Storage is allocated once in the constructor, the audio thread never
    blocks or allocates, and if the reader falls behind new audio is simply
    dropped (the audio path is never affected).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace prizm
{

template <int NumChannels>
class AudioFifo
{
public:
    explicit AudioFifo (int capacityFrames)
        : fifo (capacityFrames), data ((size_t) capacityFrames * NumChannels, 0.0f) {}

    /** Audio thread. 'channels' holds NumChannels pointers. Returns frames written. */
    int push (const float* const* channels, int numFrames) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

        auto copy = [&] (int dstStart, int count, int srcOffset)
        {
            for (int i = 0; i < count; ++i)
                for (int ch = 0; ch < NumChannels; ++ch)
                    data[(size_t) (dstStart + i) * NumChannels + (size_t) ch] = channels[ch][srcOffset + i];
        };

        copy (start1, size1, 0);
        copy (start2, size2, size1);
        fifo.finishedWrite (size1 + size2);
        return size1 + size2;
    }

    /** Reader thread. Copies up to maxFrames interleaved frames into 'dest'. */
    int pop (float* dest, int maxFrames) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (maxFrames, start1, size1, start2, size2);

        std::copy_n (data.data() + (size_t) start1 * NumChannels, (size_t) size1 * NumChannels, dest);
        std::copy_n (data.data() + (size_t) start2 * NumChannels, (size_t) size2 * NumChannels, dest + (size_t) size1 * NumChannels);

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    int getFreeSpace() const noexcept { return fifo.getFreeSpace(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};

} // namespace prizm
