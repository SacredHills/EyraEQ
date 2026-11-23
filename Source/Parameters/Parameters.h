#pragma once

#include "DSP/BandParams.h"
#include "DSP/EqCore.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace eyra::params
{

inline constexpr int kNumBands = dsp::eqBandCount;

namespace id
{
    inline const juce::String outputGain { "out" };
    inline const juce::String masterBypass { "bypass" };
} // namespace id

/** The fields of a band, in the order of the EQ core's parameter table. */
enum class BandField
{
    On = 0,        // band switched on (off: bypassed)
    Type,
    Frequency,
    Gain,
    Q,
    Slope,
    Dynamics,      // off / dynamic / spectral
    Range,
    Threshold,
    Used,          // the band exists
    Attack,
    Release,
    AutoRelease
};

/** Parameter ID for a band field. Bands are 0-indexed in code, 1-indexed in IDs ("b1freq"). */
juce::String bandParamID (int bandIndex, BandField field);

/** Default frequency for a band slot when it is first created. */
float defaultBandFrequency (int bandIndex);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** The EQ's band type for a correction's filter type, and back. */
int eqTypeFor (dsp::FilterType) noexcept;
dsp::FilterType filterTypeFor (int eqType) noexcept;

/** Message-thread helper: sets a parameter by its real (denormalised) value, notifying the host. */
void setParameterValue (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, float realValue);

/** Reads a band as the corrective EQ describes it (dynamics are not part of it). */
dsp::BandParams readBand (juce::AudioProcessorValueTreeState& state, int bandIndex);

/** Writes a band into a slot (static, switched on, in use), notifying the host. */
void writeBand (juce::AudioProcessorValueTreeState& state, int bandIndex, const dsp::BandParams& band);

/** Removes a band (not in use, gain and dynamics reset). */
void clearBand (juce::AudioProcessorValueTreeState& state, int bandIndex);

} // namespace eyra::params
