#include "Parameters.h"

namespace eyra::params
{

namespace
{
    const char* const fieldIds[] { "on", "type", "freq", "gain", "q", "slope", "dyn", "range", "thr", "use", "atk", "rel", "arel" };
    constexpr float kBandDefaults[dsp::eqBandCount] { 60, 150, 400, 1000, 2500, 5000, 9000, 15000 };

    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }
} // namespace

juce::String bandParamID (int bandIndex, BandField field)
{
    return dsp::eqId (bandIndex, fieldIds[(int) field]);
}

float defaultBandFrequency (int bandIndex)
{
    return kBandDefaults[juce::jlimit (0, dsp::eqBandCount - 1, bandIndex)];
}

int eqTypeFor (dsp::FilterType t) noexcept
{
    using T = dsp::FilterType;
    switch (t)
    {
        case T::Bell:      return dsp::eqBell;
        case T::LowShelf:  return dsp::eqLowShelf;
        case T::HighShelf: return dsp::eqHighShelf;
        case T::HighPass:  return dsp::eqLowCut;
        case T::LowPass:   return dsp::eqHighCut;
        case T::Notch:     return dsp::eqNotch;
        case T::BandPass:  return dsp::eqBandPass;
        case T::Tilt:      break;
    }
    return dsp::eqBell;
}

dsp::FilterType filterTypeFor (int t) noexcept
{
    using T = dsp::FilterType;
    switch (t)
    {
        case dsp::eqLowShelf:  return T::LowShelf;
        case dsp::eqHighShelf: return T::HighShelf;
        case dsp::eqLowCut:    return T::HighPass;
        case dsp::eqHighCut:   return T::LowPass;
        case dsp::eqNotch:     return T::Notch;
        case dsp::eqBandPass:  return T::BandPass;
        default:               break;
    }
    return T::Bell;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&] (const juce::String& pid, const juce::String& name, float lo, float hi, float def, const juce::String& unit,
                           float centre, std::function<String (float, int)> text)
    {
        NormalisableRange<float> r (lo, hi);
        if (centre > lo && centre < hi)
            r.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, 1 }, name, r, def,
                                                           AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (text))));
    };
    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
    auto msText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; };

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto name = "Band " + String (b + 1) + " ";
        auto pid = [b] (BandField f) { return bandParamID (b, f); };
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid (BandField::On), 1 }, name + "on", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (BandField::Type), 1 }, name + "type", dsp::eqTypeNames, 0));
        floatParam (pid (BandField::Frequency), name + "freq", 10.0f, 30000.0f, kBandDefaults[b], "Hz", 1000.0f, [] (float v, int) { return hzText (v); });
        floatParam (pid (BandField::Gain), name + "gain", -dsp::eqGainRange, dsp::eqGainRange, 0.0f, "dB", 0.0f, dbText);
        floatParam (pid (BandField::Q), name + "Q", 0.1f, 30.0f, 1.0f, "", 1.5f, [] (float v, int) { return String (v, 2); });
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (BandField::Slope), 1 }, name + "slope", dsp::eqSlopeNames, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (BandField::Dynamics), 1 }, name + "dynamics", dsp::eqDynNames, 0));
        floatParam (pid (BandField::Range), name + "dyn range", -dsp::eqGainRange, dsp::eqGainRange, -6.0f, "dB", 0.0f, dbText);
        floatParam (pid (BandField::Threshold), name + "threshold", -80.0f, 0.0f, -24.0f, "dB", -24.0f, dbText);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid (BandField::Used), 1 }, name + "in use", false));
        floatParam (pid (BandField::Attack), name + "attack", 0.5f, 200.0f, 10.0f, "ms", 15.0f, msText);
        floatParam (pid (BandField::Release), name + "release", 5.0f, 2000.0f, 120.0f, "ms", 150.0f, msText);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid (BandField::AutoRelease), 1 }, name + "auto release", false));
    }

    floatParam (id::outputGain, "Output", -18.0f, 18.0f, 0.0f, "dB", 0.0f, dbText);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::masterBypass, 1 }, "Bypass", false));
    return layout;
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

namespace
{
    float real (juce::AudioProcessorValueTreeState& s, int band, BandField f)
    {
        auto* p = s.getParameter (bandParamID (band, f));
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
} // namespace

dsp::BandParams readBand (juce::AudioProcessorValueTreeState& state, int b)
{
    dsp::BandParams p;
    p.used = real (state, b, BandField::Used) >= 0.5f;
    p.bypassed = real (state, b, BandField::On) < 0.5f;
    p.type = filterTypeFor (juce::roundToInt (real (state, b, BandField::Type)));
    p.frequency = real (state, b, BandField::Frequency);
    p.gainDb = real (state, b, BandField::Gain);
    p.q = real (state, b, BandField::Q);
    p.slopeIndex = juce::jlimit (0, 7, juce::roundToInt (real (state, b, BandField::Slope)));
    return p;
}

void writeBand (juce::AudioProcessorValueTreeState& state, int b, const dsp::BandParams& band)
{
    auto set = [&] (BandField f, float v) { setParameterValue (state, bandParamID (b, f), v); };
    set (BandField::Type, (float) eqTypeFor (band.type));
    set (BandField::Frequency, (float) band.frequency);
    set (BandField::Gain, (float) band.gainDb);
    set (BandField::Q, (float) band.q);
    set (BandField::Slope, (float) band.slopeIndex);
    set (BandField::Dynamics, 0.0f);
    set (BandField::On, band.bypassed ? 0.0f : 1.0f);
    set (BandField::Used, 1.0f);
}

void clearBand (juce::AudioProcessorValueTreeState& state, int b)
{
    setParameterValue (state, bandParamID (b, BandField::Used), 0.0f);
    setParameterValue (state, bandParamID (b, BandField::Gain), 0.0f);
    setParameterValue (state, bandParamID (b, BandField::Dynamics), 0.0f);
}

} // namespace eyra::params
