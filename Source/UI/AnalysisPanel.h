#pragma once

#include "EditorContext.h"

namespace eyra::ui
{

juce::Colour colourForSeverity (analysis::Severity);

/**
    The analysis findings with the EQ move proposed for each (or why it is left alone). Rows with a
    move have a tick box; clicking a row highlights its finding on the spectrum.
*/
class SuggestionList final : public juce::Component
{
public:
    explicit SuggestionList (EditorContext& context);

    void setSuggestions (std::shared_ptr<const analysis::AnalysisResult>, std::vector<correction::Suggestion>);
    const std::vector<correction::Suggestion>& getSuggestions() const noexcept { return suggestions; }

    /** Bands of the ticked suggestions, in list order. */
    std::vector<dsp::BandParams> checkedBands() const;
    int numChecked() const;

    int getIdealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onSelectionChanged, onCheckedChanged;

private:
    int rowAt (int y) const;
    int rowHeight (int index, int width) const;
    juce::String detailFor (int index) const;
    juce::String headingFor (int index) const;

    EditorContext& ctx;
    std::shared_ptr<const analysis::AnalysisResult> result;
    std::vector<correction::Suggestion> suggestions;
    std::vector<bool> checked;
    int hovered = -1;
};

/**
    Side panel: choose the instrument, listen and analyse, then turn the findings into real EQ bands
    (applied automatically or approved one by one).
*/
class AnalysisPanel final : public juce::Component
{
public:
    explicit AnalysisPanel (EditorContext& context);

    /** Called from the editor timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void layoutList();
    void settingsChanged();
    void regenerate();
    void applyChecked();
    juce::String summaryText() const;

    EditorContext& ctx;
    juce::TextButton primaryButton, secondaryButton, fileButton { "Analyze file..." };
    juce::ComboBox instrumentBox, modeBox, strengthBox;
    juce::Slider amountSlider, countSlider;
    juce::ToggleButton preserveToggle { "Preserve character" }, highPassToggle { "Auto high-pass" },
                       autoApplyToggle { "Apply automatically after analysis" };
    juce::TextButton applyButton { "Apply corrections" }, resetButton { "Reset" };
    SuggestionList list;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;

    int shownVersion = -1;
    analysis::AnalysisController::State shownState = analysis::AnalysisController::State::Idle;
    juce::Rectangle<int> statusArea, footerArea, instrumentLabelArea, correctionHeaderArea, amountLabelArea, countLabelArea;
    juce::String applyMessage;
    bool isStandalone = false;
};

} // namespace eyra::ui
