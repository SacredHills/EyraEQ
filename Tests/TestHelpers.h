#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <numbers>

namespace eyra::test
{

/** Speed assertions are only meaningful in optimised builds without sanitizers. */
#if JUCE_DEBUG || defined (__SANITIZE_ADDRESS__) || defined (__SANITIZE_THREAD__)
 #define EYRA_TIMING_ASSERTIONS 0
#elif defined (__has_feature)
 #if __has_feature (address_sanitizer) || __has_feature (thread_sanitizer)
  #define EYRA_TIMING_ASSERTIONS 0
 #endif
#endif
#ifndef EYRA_TIMING_ASSERTIONS
 #define EYRA_TIMING_ASSERTIONS 1
#endif
inline constexpr bool kTimingAssertions = EYRA_TIMING_ASSERTIONS != 0;

/** Generous timeout for waiting on background work (returns as soon as the work is done). */
inline constexpr int kBackgroundTimeoutMs = 180000;

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
