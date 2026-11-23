#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace eyra::ui
{

/** State shared by the editor's components: the processor and the band selection. Message thread. */
struct EditorContext
{
    explicit EditorContext (EyraEQAudioProcessor& p) : processor (p), state (p.apvts) {}

    EyraEQAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    /** Selected band (-1: none). */
    int selectedBand = -1;

    /** Latest analysis result shown in the panel and on the spectrum, and the highlighted finding. */
    std::shared_ptr<const analysis::AnalysisResult> analysis;
    int selectedFinding = -1;

    juce::RangedAudioParameter* param (int band, params::BandField field) const
    {
        return state.getParameter (params::bandParamID (band, field));
    }

    float value (int band, params::BandField field) const
    {
        auto* p = param (band, field);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    void setValue (int band, params::BandField field, float realValue)
    {
        params::setParameterValue (state, params::bandParamID (band, field), realValue);
    }

    dsp::BandParams readBand (int band) const { return params::readBand (state, band); }
};

} // namespace eyra::ui
