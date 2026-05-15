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
        int s1, n1, s2, n2;
        fifo.prepareToWrite(numSamples, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) buffer[static_cast<size_t>(s1 + i)] = samples[i];
        for (int i = 0; i < n2; ++i) buffer[static_cast<size_t>(s2 + i)] = samples[n1 + i];
        fifo.finishedWrite(n1 + n2);
    }

    int read(float* destination, int maxSamples) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead(maxSamples, s1, n1, s2, n2);
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
