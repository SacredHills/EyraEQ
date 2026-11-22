#pragma once

#include "PluginProcessor.h"
#include "UI/AnalysisPanel.h"
#include "UI/BandControlPanel.h"
#include "UI/EditorContext.h"
#include "UI/EyraLookAndFeel.h"
#include "UI/SpectrumDisplay.h"
#include "UI/TopBar.h"

/** In-window prompt for naming a preset (avoids separate top-level windows inside hosts). */
class SavePresetOverlay final : public juce::Component
{
public:
    SavePresetOverlay();
    void show (const juce::String& initialName);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (const juce::String&)> onSave;

private:
    void commit();
    juce::TextEditor nameEditor;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
    juce::Rectangle<int> card;
};

class EyraEQAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EyraEQAudioProcessorEditor (EyraEQAudioProcessor&);
    ~EyraEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    eyra::ui::SpectrumDisplay& getSpectrumDisplay() noexcept { return display; }
    eyra::ui::EditorContext& getContext() noexcept { return context; }
    void refreshNow() { timerCallback(); topBar.refresh(); bandPanel.refresh(); analysisPanel.refresh(); }

private:
    void timerCallback() override;

    EyraEQAudioProcessor& processorRef;
    eyra::ui::EyraLookAndFeel lookAndFeel;
    eyra::ui::EditorContext context;

    eyra::ui::TopBar topBar;
    eyra::ui::SpectrumDisplay display;
    eyra::ui::BandControlPanel bandPanel;
    eyra::ui::AnalysisPanel analysisPanel;
    SavePresetOverlay saveOverlay;
    juce::TooltipWindow tooltips { this, 700 };

    double lastTick = 0.0;
    int slowCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EyraEQAudioProcessorEditor)
};
