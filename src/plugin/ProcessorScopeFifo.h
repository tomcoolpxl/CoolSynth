#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace coolsynth::plugin
{

class ProcessorScopeFifo final
{
public:
    static constexpr int capacity = 16384;

    ProcessorScopeFifo() : buffer(capacity, 0.0f), fifo(capacity) {}

    void write(const float* samples, int numSamples) noexcept
    {
        if (numSamples > capacity)
        {
            samples += numSamples - capacity;
            numSamples = capacity;
        }

        int s1, n1, s2, n2;
        fifo.prepareToWrite(numSamples, s1, n1, s2, n2);

        // If we don't have enough space, we must advance the read pointer to make room.
        // This effectively makes it an "overwrite-oldest" ring buffer.
        if (n1 + n2 < numSamples)
        {
            fifo.finishedRead(numSamples - (n1 + n2));
            fifo.prepareToWrite(numSamples, s1, n1, s2, n2);
        }

        for (int i = 0; i < n1; ++i) buffer[static_cast<size_t>(s1 + i)] = samples[i];
        for (int i = 0; i < n2; ++i) buffer[static_cast<size_t>(s2 + i)] = samples[n1 + i];
        fifo.finishedWrite(n1 + n2);
    }

    int read(float* destination, int maxSamples) noexcept
    {
        int available = fifo.getNumReady();
        if (available <= 0)
            return 0;

        // If we have massive buildup, skip the oldest stuff so the scope is live.
        if (available > maxSamples * 2)
        {
            const int toSkip = available - maxSamples;
            fifo.finishedRead(toSkip);
            available = maxSamples;
        }

        const int numToRead = std::min(available, maxSamples);
        int s1, n1, s2, n2;
        fifo.prepareToRead(numToRead, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) destination[i]       = buffer[static_cast<size_t>(s1 + i)];
        for (int i = 0; i < n2; ++i) destination[n1 + i]  = buffer[static_cast<size_t>(s2 + i)];
        fifo.finishedRead(n1 + n2);
        return n1 + n2;
    }

    void clear() noexcept { fifo.reset(); }

private:
    std::vector<float> buffer;
    juce::AbstractFifo fifo;
};

} // namespace coolsynth::plugin
