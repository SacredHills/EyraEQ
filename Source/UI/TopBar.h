#pragma once

#include "EditorContext.h"

namespace eyra::ui
{

class IconButton final : public juce::Button
{
public:
    enum class Icon { Undo, Redo, Power, Settings, More, Previous, Next };

    IconButton (const juce::String& name, Icon icon);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    juce::Colour onColour;

private:
    Icon icon;
};

/** Header: name, preset browser, undo / redo, A / B, edit menu, master bypass and settings. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (EditorContext& context);

    /** Called from the editor timer to update button states and the preset name. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onSavePresetRequested;
    std::function<void (const EyraUIState&)> onUIStateChanged;
    std::function<void (int width, int height)> onWindowSizeRequested;
    std::function<void (bool open)> onAnalysisPanelToggled;

    void copySettingsToClipboard();
    void pasteSettingsFromClipboard();

private:
    void showPresetMenu();
    void showEditMenu();
    void showSettingsMenu();

    EditorContext& ctx;

    IconButton prevButton { "Previous preset", IconButton::Icon::Previous };
    IconButton nextButton { "Next preset", IconButton::Icon::Next };
    juce::TextButton presetButton;
    IconButton undoButton { "Undo", IconButton::Icon::Undo };
    IconButton redoButton { "Redo", IconButton::Icon::Redo };
    juce::TextButton aButton { "A" }, bButton { "B" }, copyABButton;
    juce::TextButton analysisButton { "Analysis" };
    IconButton editButton { "Edit", IconButton::Icon::More };
    IconButton bypassButton { "Bypass", IconButton::Icon::Power };
    IconButton settingsButton { "Settings", IconButton::Icon::Settings };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::Rectangle<int> logoArea;
};

} // namespace eyra::ui
