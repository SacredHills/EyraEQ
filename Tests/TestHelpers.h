#pragma once

#include "DSP/BandParams.h"
#include "DSP/EqCore.h"
#include "Parameters/Parameters.h"

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

/** A filter to run a test signal through: the same section design the EQ itself uses. */
struct FilterSpec
{
    dsp::FilterType type = dsp::FilterType::Bell;
    double frequency = 1000.0, gainDb = 0.0, q = 0.7071, slopeIndex = 1;
};

inline void applyFilter (std::vector<float>& x, const FilterSpec& spec, double sr, int start = 0, int end = -1)
{
    dsp::EqBand b;
    b.type = (float) params::eqTypeFor (spec.type);
    b.freq = (float) spec.frequency;
    b.q = (float) spec.q;
    b.slope = (float) spec.slopeIndex;
    dsp::EqBandFilter f;
    f.design (b, sr, (float) spec.gainDb);
    if (end < 0)
        end = (int) x.size();
    f.process (x.data() + start, end - start);
}

} // namespace eyra::test
