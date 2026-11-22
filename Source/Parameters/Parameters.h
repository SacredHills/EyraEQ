#pragma once

#include "DSP/EQBand.h"
#include "DSP/EQEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace eyra::params
{

inline constexpr int kNumBands = dsp::EQEngine::kMaxBands;

namespace id
{
    inline const juce::String inputGain { "input_gain" };
    inline const juce::String outputGain { "output_gain" };
    inline const juce::String masterBypass { "bypass" };
} // namespace id

enum class BandField
{
    Used,
    Bypass,
    Type,
    Frequency,
    Gain,
    Q,
    Slope,
    Placement
};

/** Parameter ID for a band field. Bands are 0-indexed in code, 1-indexed in IDs ("b1_freq"). */
juce::String bandParamID (int bandIndex, BandField field);

juce::StringArray filterTypeNames();
juce::StringArray slopeNames();
juce::StringArray placementNames();

/** Default frequency for a band slot when it is first created. */
float defaultBandFrequency (int bandIndex);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Lock-free access to the raw parameter values for the audio thread. */
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState& state);

    dsp::BandParams readBand (int bandIndex) const noexcept;
    float inputGainDb() const noexcept { return inputGain->load(); }
    float outputGainDb() const noexcept { return outputGain->load(); }
    bool masterBypass() const noexcept { return bypass->load() >= 0.5f; }

private:
    struct BandPointers
    {
        std::atomic<float>* used = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* placement = nullptr;
    };

    std::array<BandPointers, kNumBands> bands;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* bypass = nullptr;
};

/** Message-thread helper: sets a parameter by its real (denormalised) value, notifying the host. */
void setParameterValue (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, float realValue);

} // namespace eyra::params
