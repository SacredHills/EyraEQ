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

    /** Selected band indices; the most recently selected one is the "primary" band. */
    juce::SelectedItemSet<int> selection;

    int primaryBand() const
    {
        const int n = selection.getNumSelected();
        return n > 0 ? selection.getSelectedItem (n - 1) : -1;
    }

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

    dsp::BandParams readBand (int band) const
    {
        using params::BandField;
        dsp::BandParams p;
        p.used = value (band, BandField::Used) >= 0.5f;
        p.bypassed = value (band, BandField::Bypass) >= 0.5f;
        p.type = (dsp::FilterType) juce::jlimit (0, dsp::kNumFilterTypes - 1, juce::roundToInt (value (band, BandField::Type)));
        p.frequency = value (band, BandField::Frequency);
        p.gainDb = value (band, BandField::Gain);
        p.q = value (band, BandField::Q);
        p.slopeIndex = juce::jlimit (0, 7, juce::roundToInt (value (band, BandField::Slope)));
        p.placement = (dsp::StereoPlacement) juce::jlimit (0, 4, juce::roundToInt (value (band, BandField::Placement)));
        return p;
    }

    /** Creates a band in the first free slot. Returns its index, or -1 if all slots are in use. */
    int createBand (dsp::FilterType type, float frequency, float gainDb)
    {
        using params::BandField;
        for (int b = 0; b < params::kNumBands; ++b)
        {
            if (value (b, BandField::Used) >= 0.5f)
                continue;

            processor.history.beginEdit();
            state::EditHistory::ScopedSuspend suspend (processor.history);
            setValue (b, BandField::Type, (float) type);
            setValue (b, BandField::Frequency, frequency);
            setValue (b, BandField::Gain, dsp::typeUsesGain (type) ? gainDb : 0.0f);
            setValue (b, BandField::Q, 0.7071f);
            setValue (b, BandField::Slope, (float) dsp::kDefaultSlopeIndex);
            setValue (b, BandField::Placement, 0.0f);
            setValue (b, BandField::Bypass, 0.0f);
            setValue (b, BandField::Used, 1.0f);
            return b;
        }
        return -1;
    }

    void deleteBands (const juce::Array<int>& bands)
    {
        if (bands.isEmpty())
            return;

        processor.history.beginEdit();
        state::EditHistory::ScopedSuspend suspend (processor.history);
        for (int b : bands)
        {
            setValue (b, params::BandField::Used, 0.0f);
            selection.deselect (b);
            if (processor.soloBand.load() == b)
                processor.soloBand = -1;
        }
    }
};

} // namespace eyra::ui
