#include "Parameters.h"

#include <cmath>

namespace eyra::params
{

namespace
{
    const char* fieldSuffix (BandField f)
    {
        switch (f)
        {
            case BandField::Used:      return "used";
            case BandField::Bypass:    return "bypass";
            case BandField::Type:      return "type";
            case BandField::Frequency: return "freq";
            case BandField::Gain:      return "gain";
            case BandField::Q:         return "q";
            case BandField::Slope:     return "slope";
            case BandField::Placement: return "place";
        }
        return "";
    }

    juce::NormalisableRange<float> logRange (float minValue, float maxValue)
    {
        const float ratio = maxValue / minValue;
        return { minValue, maxValue,
                 [=] (float, float, float x) { return minValue * std::pow (ratio, x); },
                 [=] (float, float, float v) { return std::log (v / minValue) / std::log (ratio); },
                 [=] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
    }

    juce::String formatFrequency (float hz, int)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return (hz < 100.0f ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz))) + " Hz";
    }

    float parseFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float v = t.getFloatValue();
        return t.containsChar ('k') ? v * 1000.0f : v;
    }
} // namespace

juce::String bandParamID (int bandIndex, BandField field)
{
    return "b" + juce::String (bandIndex + 1) + "_" + fieldSuffix (field);
}

juce::StringArray filterTypeNames()
{
    return { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "Tilt" };
}

juce::StringArray slopeNames()
{
    juce::StringArray names;
    for (auto s : dsp::kSlopesDbPerOct)
        names.add (juce::String (s) + " dB/oct");
    return names;
}

juce::StringArray placementNames()
{
    return { "Stereo", "Left", "Right", "Mid", "Side" };
}

float defaultBandFrequency (int bandIndex)
{
    // Spread unused slots logarithmically between 40 Hz and 16 kHz.
    const float t = (float) bandIndex / (float) (kNumBands - 1);
    return 40.0f * std::pow (400.0f, t);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < kNumBands; ++b)
    {
        const String prefix = "Band " + String (b + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), " | ");

        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandParamID (b, BandField::Used), 1 }, prefix + "Enabled", false));
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandParamID (b, BandField::Bypass), 1 }, prefix + "Bypass", false));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandParamID (b, BandField::Type), 1 }, prefix + "Type", filterTypeNames(), 0));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamID (b, BandField::Frequency), 1 }, prefix + "Frequency",
            logRange ((float) dsp::kMinFrequency, (float) dsp::kMaxFrequency), defaultBandFrequency (b),
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction (formatFrequency)
                .withValueFromStringFunction (parseFrequency)));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamID (b, BandField::Gain), 1 }, prefix + "Gain",
            NormalisableRange<float> ((float) -dsp::kMaxGainDb, (float) dsp::kMaxGainDb, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " dB"; })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamID (b, BandField::Q), 1 }, prefix + "Q",
            logRange ((float) dsp::kMinQ, (float) dsp::kMaxQ), 0.7071f,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return String (v, v < 1.0f ? 3 : 2); })));

        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandParamID (b, BandField::Slope), 1 }, prefix + "Slope", slopeNames(), dsp::kDefaultSlopeIndex));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandParamID (b, BandField::Placement), 1 }, prefix + "Stereo Placement", placementNames(), 0));

        layout.add (std::move (group));
    }

    auto gainAttributes = AudioParameterFloatAttributes().withLabel ("dB")
                              .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::inputGain, 1 }, "Input Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, gainAttributes));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::outputGain, 1 }, "Output Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, gainAttributes));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::masterBypass, 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& state)
{
    auto get = [&state] (const juce::String& paramID)
    {
        auto* p = state.getRawParameterValue (paramID);
        jassert (p != nullptr);
        return p;
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& bp = bands[(size_t) b];
        bp.used = get (bandParamID (b, BandField::Used));
        bp.bypass = get (bandParamID (b, BandField::Bypass));
        bp.type = get (bandParamID (b, BandField::Type));
        bp.freq = get (bandParamID (b, BandField::Frequency));
        bp.gain = get (bandParamID (b, BandField::Gain));
        bp.q = get (bandParamID (b, BandField::Q));
        bp.slope = get (bandParamID (b, BandField::Slope));
        bp.placement = get (bandParamID (b, BandField::Placement));
    }

    inputGain = get (id::inputGain);
    outputGain = get (id::outputGain);
    bypass = get (id::masterBypass);
}

dsp::BandParams ParameterReader::readBand (int bandIndex) const noexcept
{
    const auto& bp = bands[(size_t) bandIndex];
    dsp::BandParams p;
    p.used = bp.used->load() >= 0.5f;
    p.bypassed = bp.bypass->load() >= 0.5f;
    p.type = (dsp::FilterType) juce::jlimit (0, dsp::kNumFilterTypes - 1, (int) std::lround (bp.type->load()));
    p.frequency = bp.freq->load();
    p.gainDb = bp.gain->load();
    p.q = bp.q->load();
    p.slopeIndex = juce::jlimit (0, (int) dsp::kSlopesDbPerOct.size() - 1, (int) std::lround (bp.slope->load()));
    p.placement = (dsp::StereoPlacement) juce::jlimit (0, dsp::kNumStereoPlacements - 1, (int) std::lround (bp.placement->load()));
    return p;
}

void setParameterValue (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, float realValue)
{
    if (auto* p = state.getParameter (paramID))
    {
        const float normalised = p->convertTo0to1 (realValue);
        if (std::abs (p->getValue() - normalised) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    }
}

} // namespace eyra::params
