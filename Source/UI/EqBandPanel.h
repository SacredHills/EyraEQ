#pragma once

#include "EditorContext.h"

namespace eyra::ui
{

/**
    Controls for the selected band (type, slope, frequency, gain, Q, dynamics with threshold, range, attack and release,
    Listen) and the output gain. Bound to the parameters through attachments, so host automation and undo stay in sync.
*/
class EqBandPanel final : public juce::Component
{
public:
    explicit EqBandPanel (EditorContext& context);
    ~EqBandPanel() override;

    /** Rebinds to the selected band (called when the selection changes). */
    void showBand (int band);

    /** Called from the editor timer: enablement of the controls, the Listen button. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
    };

    void placeKnob (int k, juce::Rectangle<int> cell);
    void syncListen();

    EditorContext& ctx;
    int bound = -1;

    juce::Label bandLabel;
    juce::ComboBox typeBox, slopeBox, dynBox;
    juce::ToggleButton autoRelButton { "Auto release" };
    juce::TextButton onButton { "On" }, removeButton { "Remove" }, listenButton { "Listen" };
    std::array<Knob, 8> knobs; // 0..6 band (frequency, gain, Q, threshold, range, attack, release), 7 output
    std::unique_ptr<juce::ComboBoxParameterAttachment> typeAttach, slopeAttach, dynAttach;
    std::unique_ptr<juce::ButtonParameterAttachment> onAttach, autoRelAttach;
    juce::Rectangle<int> bandPanel, globalPanel, dynLabelArea;
};

} // namespace eyra::ui
