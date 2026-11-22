#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numbers>

namespace eyra::test
{

inline void fillSine (juce::AudioBuffer<float>& buffer, double frequency, double sampleRate, float amplitude, int64_t& phaseSample, int channel = -1)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float v = amplitude * (float) std::sin (2.0 * std::numbers::pi * frequency * (double) (phaseSample + i) / sampleRate);
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            if (channel < 0 || c == channel)
                buffer.setSample (c, i, v);
    }
    phaseSample += buffer.getNumSamples();
}

inline double rms (const float* data, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) data[i] * data[i];
    return std::sqrt (sum / std::max (1, n));
}

inline double toDb (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-20)); }

} // namespace eyra::test
