#include "PluginEditor.h"

using namespace eyra;

namespace
{
    constexpr int kTopBarHeight = 46;
    constexpr int kBandPanelHeight = 132;
    constexpr int kAnalysisPanelWidth = 300;
    constexpr int kMinWidth = 860, kMinHeight = 520;
    constexpr int kMaxWidth = 3200, kMaxHeight = 2000;
} // namespace

//==============================================================================
SavePresetOverlay::SavePresetOverlay()
{
    addAndMakeVisible (nameEditor);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);
    nameEditor.setFont (ui::uiFont (15.0f));
    nameEditor.onReturnKey = [this] { commit(); };
    nameEditor.onEscapeKey = [this] { setVisible (false); };
    saveButton.onClick = [this] { commit(); };
    cancelButton.onClick = [this] { setVisible (false); };
    setVisible (false);
}

void SavePresetOverlay::show (const juce::String& initialName)
{
    nameEditor.setText (initialName == "Default" ? juce::String() : initialName, false);
    setVisible (true);
    toFront (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void SavePresetOverlay::commit()
{
    const auto name = nameEditor.getText().trim();
    if (name.isEmpty())
        return;
    setVisible (false);
    if (onSave)
        onSave (name);
}

bool SavePresetOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return false;
}

void SavePresetOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    g.setColour (ui::colours::panelRaised);
    g.fillRoundedRectangle (card.toFloat(), 6.0f);
    g.setColour (ui::colours::outline);
    g.drawRoundedRectangle (card.toFloat(), 6.0f, 1.0f);
    g.setColour (ui::colours::text);
    g.setFont (ui::uiFont (16.0f, true));
    g.drawText ("Save preset", card.reduced (16, 12).removeFromTop (22), juce::Justification::centredLeft, false);
}

void SavePresetOverlay::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (360, 140);
    auto inner = card.reduced (16, 12);
    inner.removeFromTop (30);
    nameEditor.setBounds (inner.removeFromTop (30));
    inner.removeFromTop (14);
    auto buttons = inner.removeFromTop (28);
    saveButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

//==============================================================================
EyraEQAudioProcessorEditor::EyraEQAudioProcessorEditor (EyraEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), context (p), topBar (context), display (context), bandPanel (context), analysisPanel (context)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (topBar);
    addAndMakeVisible (display);
    addAndMakeVisible (bandPanel);
    addChildComponent (analysisPanel);
    analysisPanel.setVisible (p.getUIState().analysisPanelOpen);
    addChildComponent (saveOverlay);

    topBar.onSavePresetRequested = [this] { saveOverlay.show (processorRef.presets.getCurrentPresetName()); };
    topBar.onUIStateChanged = [this] (const EyraUIState& s) { display.applyUIState (s); };
    topBar.onWindowSizeRequested = [this] (int w, int h) { setSize (w, h); };
    topBar.onAnalysisPanelToggled = [this] (bool open)
    {
        analysisPanel.setVisible (open);
        resized();
    };
    saveOverlay.onSave = [this] (const juce::String& name) { processorRef.presets.saveUserPreset (name); };

    const auto ui = processorRef.getUIState();
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (juce::jlimit (kMinWidth, kMaxWidth, ui.editorWidth), juce::jlimit (kMinHeight, kMaxHeight, ui.editorHeight));

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

EyraEQAudioProcessorEditor::~EyraEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EyraEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void EyraEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (kTopBarHeight));
    bandPanel.setBounds (area.removeFromBottom (kBandPanelHeight));
    if (analysisPanel.isVisible())
        analysisPanel.setBounds (area.removeFromRight (kAnalysisPanelWidth));
    display.setBounds (area);
    saveOverlay.setBounds (getLocalBounds());

    auto ui = processorRef.getUIState();
    ui.editorWidth = getWidth();
    ui.editorHeight = getHeight();
    processorRef.setUIState (ui);
}

void EyraEQAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    display.tick ((now - lastTick) * 0.001);
    lastTick = now;

    // Controls that don't need 60 Hz updates.
    if (++slowCounter >= 6)
    {
        slowCounter = 0;
        topBar.refresh();
        bandPanel.refresh();
        analysisPanel.refresh();
    }
}

bool EyraEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown();
    const int code = key.getKeyCode();

    if (cmd && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown())
            processorRef.history.redo();
        else
            processorRef.history.undo();
        return true;
    }
    if (cmd && (code == 'Y' || code == 'y'))
    {
        processorRef.history.redo();
        return true;
    }
    if (cmd && (code == 'C' || code == 'c'))
    {
        topBar.copySettingsToClipboard();
        return true;
    }
    if (cmd && (code == 'V' || code == 'v'))
    {
        topBar.pasteSettingsFromClipboard();
        return true;
    }
    if (cmd && (code == 'A' || code == 'a'))
    {
        context.selection.deselectAll();
        for (int b = 0; b < params::kNumBands; ++b)
            if (context.value (b, params::BandField::Used) >= 0.5f)
                context.selection.addToSelection (b);
        display.repaint();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        context.deleteBands (context.selection.getItemArray());
        display.repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        context.selection.deselectAll();
        display.repaint();
        return true;
    }
    return false;
}
