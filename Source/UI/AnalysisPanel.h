#pragma once

#include "EditorContext.h"

namespace eyra::ui
{

juce::Colour colourForSeverity (analysis::Severity);

/** Scrollable list of findings; clicking a row highlights it on the spectrum. */
class FindingsList final : public juce::Component
{
public:
    explicit FindingsList (EditorContext& context);

    void setFindings (std::shared_ptr<const analysis::AnalysisResult>);
    int getIdealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onSelectionChanged;

private:
    int rowAt (int y) const;
    int rowHeight (int index, int width) const;

    EditorContext& ctx;
    std::shared_ptr<const analysis::AnalysisResult> result;
    int hovered = -1;
};

/**
    Collapsible side panel for the listen-then-analyse workflow: start / stop, how much audio has been
    heard, a summary of the source, and the list of findings. Shows only real analysis results.
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
    juce::String summaryText() const;

    EditorContext& ctx;
    juce::TextButton primaryButton, secondaryButton, fileButton { "Analyze file..." };
    FindingsList list;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;

    int shownVersion = -1;
    analysis::AnalysisController::State shownState = analysis::AnalysisController::State::Idle;
    juce::Rectangle<int> statusArea, footerArea;
    bool isStandalone = false;
};

} // namespace eyra::ui
