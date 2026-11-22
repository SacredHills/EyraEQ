#pragma once

#include "EditorContext.h"

namespace eyra::ui
{

/** Knob with a caption above it and the parameter's value text below. */
class LabelledKnob final : public juce::Component
{
public:
    explicit LabelledKnob (const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String caption;
};

/**
    Detailed controls for the primary selected band, plus the global input / output gain.
    Controls are bound to the band's parameters through APVTS attachments, so host automation,
    undo and the spectrum display stay in sync.
*/
class BandControlPanel final : public juce::Component
{
public:
    explicit BandControlPanel (EditorContext& context);

    /** Called from the editor timer: rebinds to the current primary band and updates enablement. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void bindToBand (int band);

    EditorContext& ctx;
    int boundBand = -1;

    juce::ComboBox typeBox, slopeBox, placementBox;
    LabelledKnob freqKnob { "FREQUENCY" }, gainKnob { "GAIN" }, qKnob { "Q" };
    juce::TextButton bypassButton { "Bypass" }, soloButton { "Solo" }, deleteButton { "Delete" };
    LabelledKnob inputKnob { "INPUT" }, outputKnob { "OUTPUT" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment, placementAttachment;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    SliderAttachment inputAttachment, outputAttachment;

    juce::Rectangle<int> bandArea, globalArea;
};

} // namespace eyra::ui
