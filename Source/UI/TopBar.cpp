#include "TopBar.h"

#include "EyraLookAndFeel.h"

namespace eyra::ui
{

IconButton::IconButton (const juce::String& name, Icon i) : juce::Button (name), onColour (colours::accent), icon (i)
{
    setTooltip (name);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    using namespace juce;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (colours::control.brighter (down ? 0.15f : (over && isEnabled() ? 0.07f : 0.0f)));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const auto c = bounds.getCentre();
    const float s = jmin (bounds.getWidth(), bounds.getHeight()) * 0.28f;
    Colour fg = isEnabled() ? colours::text : colours::textFaint;
    if (icon == Icon::Power)
        fg = getToggleState() ? colours::warning : colours::accent; // amber when bypassed

    g.setColour (fg);
    const PathStrokeType stroke (1.6f, PathStrokeType::curved, PathStrokeType::rounded);
    Path p;

    switch (icon)
    {
        case Icon::Undo:
        case Icon::Redo:
        {
            const float dir = icon == Icon::Undo ? 1.0f : -1.0f;
            p.addCentredArc (c.x, c.y + s * 0.25f, s, s * 0.85f, 0.0f, -dir * MathConstants<float>::pi * 0.5f, dir * MathConstants<float>::pi * 0.55f, true);
            g.strokePath (p, stroke);
            Path head;
            const float hx = c.x - dir * s, hy = c.y + s * 0.25f;
            head.addTriangle (hx - dir * 0.0f, hy - s * 0.75f, hx - dir * s * 0.55f, hy, hx + dir * s * 0.55f, hy);
            g.fillPath (head, AffineTransform::translation (0.0f, -s * 0.05f));
            break;
        }
        case Icon::Power:
        {
            p.addCentredArc (c.x, c.y + s * 0.1f, s, s, 0.0f, MathConstants<float>::pi * 0.22f, MathConstants<float>::pi * 1.78f, true);
            p.startNewSubPath (c.x, c.y - s * 1.15f);
            p.lineTo (c.x, c.y - s * 0.1f);
            g.strokePath (p, stroke);
            break;
        }
        case Icon::Settings:
        {
            // Three faders
            for (int i = 0; i < 3; ++i)
            {
                const float y = c.y + (float) (i - 1) * s * 0.85f;
                p.startNewSubPath (c.x - s * 1.2f, y);
                p.lineTo (c.x + s * 1.2f, y);
                const float kx = c.x + s * (i == 0 ? -0.5f : (i == 1 ? 0.6f : -0.1f));
                g.fillEllipse (kx - s * 0.32f, y - s * 0.32f, s * 0.64f, s * 0.64f);
            }
            g.strokePath (p, PathStrokeType (1.2f));
            break;
        }
        case Icon::More:
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (c.x + (float) i * s * 0.9f - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
            break;
        case Icon::Previous:
        case Icon::Next:
        {
            const float dir = icon == Icon::Next ? 1.0f : -1.0f;
            p.startNewSubPath (c.x - dir * s * 0.4f, c.y - s * 0.8f);
            p.lineTo (c.x + dir * s * 0.4f, c.y);
            p.lineTo (c.x - dir * s * 0.4f, c.y + s * 0.8f);
            g.strokePath (p, stroke);
            break;
        }
    }
}

//==============================================================================
TopBar::TopBar (EditorContext& context) : ctx (context)
{
    for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &presetButton, &undoButton, &redoButton,
                                                            &aButton, &bButton, &copyABButton, &editButton, &bypassButton, &settingsButton })
        addAndMakeVisible (c);

    presetButton.setTooltip ("Presets");
    aButton.setTooltip ("Recall setting A");
    bButton.setTooltip ("Recall setting B");
    copyABButton.setTooltip ("Copy the active setting to the other slot");

    auto& proc = ctx.processor;
    prevButton.onClick = [&proc] { proc.presets.loadPrevious(); };
    nextButton.onClick = [&proc] { proc.presets.loadNext(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    undoButton.onClick = [&proc] { proc.history.undo(); };
    redoButton.onClick = [&proc] { proc.history.redo(); };
    aButton.onClick = [&proc] { if (proc.ab.getActiveSlot() != 0) proc.ab.toggle(); };
    bButton.onClick = [&proc] { if (proc.ab.getActiveSlot() != 1) proc.ab.toggle(); };
    copyABButton.onClick = [&proc] { proc.ab.copyActiveToInactive(); };
    editButton.onClick = [this] { showEditMenu(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the whole EQ");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.state, params::id::masterBypass, bypassButton);

    refresh();
}

void TopBar::refresh()
{
    auto& proc = ctx.processor;
    const auto name = proc.presets.getCurrentPresetName();
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);

    undoButton.setEnabled (proc.history.canUndo());
    redoButton.setEnabled (proc.history.canRedo());

    const int slot = proc.ab.getActiveSlot();
    aButton.setToggleState (slot == 0, juce::dontSendNotification);
    bButton.setToggleState (slot == 1, juce::dontSendNotification);
    const juce::String copyText = slot == 0 ? "A > B" : "B > A";
    if (copyABButton.getButtonText() != copyText)
        copyABButton.setButtonText (copyText);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Wordmark: a small eye-like lens mark and the name.
    auto logo = logoArea.toFloat();
    const float cy = logo.getCentreY();
    juce::Path mark;
    mark.startNewSubPath (logo.getX(), cy);
    mark.quadraticTo (logo.getX() + 11.0f, cy - 9.0f, logo.getX() + 22.0f, cy);
    mark.quadraticTo (logo.getX() + 11.0f, cy + 9.0f, logo.getX(), cy);
    g.setColour (colours::accent);
    g.strokePath (mark, juce::PathStrokeType (1.6f));
    g.fillEllipse (logo.getX() + 8.0f, cy - 3.0f, 6.0f, 6.0f);

    g.setColour (colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("EYRA", logo.withTrimmedLeft (30.0f).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour (colours::textDim);
    g.setFont (uiFont (17.0f));
    g.drawText ("EQ", logo.withTrimmedLeft (80.0f).toNearestInt(), juce::Justification::centredLeft, false);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    logoArea = area.removeFromLeft (120);

    const int h = area.getHeight();
    auto right = area.removeFromRight (h * 4 + 3 * 6 + 0);
    settingsButton.setBounds (right.removeFromRight (h));
    right.removeFromRight (6);
    bypassButton.setBounds (right.removeFromRight (h));
    right.removeFromRight (6);
    editButton.setBounds (right.removeFromRight (h));

    area.removeFromRight (12);
    copyABButton.setBounds (area.removeFromRight (56));
    area.removeFromRight (4);
    bButton.setBounds (area.removeFromRight (30));
    area.removeFromRight (2);
    aButton.setBounds (area.removeFromRight (30));
    area.removeFromRight (12);
    redoButton.setBounds (area.removeFromRight (h));
    area.removeFromRight (4);
    undoButton.setBounds (area.removeFromRight (h));

    // Preset browser centred in the remaining space.
    const int presetW = juce::jmin (300, area.getWidth() - 2 * (h + 4) - 20);
    auto presets = area.withSizeKeepingCentre (presetW + 2 * (h + 4), h);
    prevButton.setBounds (presets.removeFromLeft (h));
    nextButton.setBounds (presets.removeFromRight (h));
    presetButton.setBounds (presets.reduced (4, 0));
}

void TopBar::copySettingsToClipboard()
{
    auto xml = ctx.processor.presets.createPresetXml (ctx.processor.presets.getCurrentPresetName());
    juce::SystemClipboard::copyTextToClipboard (xml->toString());
}

void TopBar::pasteSettingsFromClipboard()
{
    if (auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard()))
        ctx.processor.presets.applyPresetXml (*xml);
}

void TopBar::showPresetMenu()
{
    auto& presets = ctx.processor.presets;
    presets.refresh();
    const auto& entries = presets.getEntries();
    const int current = presets.getCurrentIndex();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory");
    bool userHeaderAdded = false;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (! entries[i].isFactory && ! userHeaderAdded)
        {
            menu.addSectionHeader ("User");
            userHeaderAdded = true;
        }
        menu.addItem (1 + (int) i, entries[i].name, true, (int) i == current);
    }

    menu.addSeparator();
    menu.addItem (10001, "Save preset...");
    const bool canDelete = current >= 0 && ! entries[(size_t) current].isFactory;
    menu.addItem (10002, "Delete \"" + presets.getCurrentPresetName() + "\"", canDelete);
    menu.addItem (10003, "Show preset folder");

    juce::Component::SafePointer<TopBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, current] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& p = safe->ctx.processor.presets;
                            if (result == 10001)
                            {
                                if (safe->onSavePresetRequested)
                                    safe->onSavePresetRequested();
                            }
                            else if (result == 10002)
                                p.deleteUserPreset (current);
                            else if (result == 10003)
                            {
                                auto dir = state::PresetManager::getUserPresetDirectory();
                                dir.createDirectory();
                                dir.startAsProcess();
                            }
                            else
                                p.loadPreset (result - 1);
                        });
}

void TopBar::showEditMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Copy EQ settings");
    menu.addItem (2, "Paste EQ settings");
    menu.addSeparator();
    menu.addItem (3, "Reset EQ (remove all bands)");

    juce::Component::SafePointer<TopBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editButton),
                        [safe] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                                safe->copySettingsToClipboard();
                            else if (result == 2)
                                safe->pasteSettingsFromClipboard();
                            else if (result == 3)
                            {
                                safe->ctx.processor.presets.resetToDefault();
                                safe->ctx.selection.deselectAll();
                            }
                        });
}

void TopBar::showSettingsMenu()
{
    const auto s = ctx.processor.getUIState();
    using Mode = analysis::AnalyzerSettings::Mode;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Analyzer");
    menu.addItem (1, "Show input spectrum (pre EQ)", true, s.showPreSpectrum);
    menu.addItem (2, "Show output spectrum (post EQ)", true, s.showPostSpectrum);
    menu.addItem (3, "Freeze spectrum", true, s.analyzer.freeze);

    juce::PopupMenu mode, resolution, speed, averaging, decay, slope, range, size;
    mode.addItem (10, "RMS (averaged)", true, s.analyzer.mode == Mode::RMS);
    mode.addItem (11, "Peak", true, s.analyzer.mode == Mode::Peak);

    for (int order = analysis::AnalyzerSettings::kMinOrder; order <= analysis::AnalyzerSettings::kMaxOrder; ++order)
        resolution.addItem (20 + order, juce::String (1 << order) + " points", true, s.analyzer.fftOrder == order);

    const char* speedNames[] = { "Slow", "Normal", "Fast" };
    for (int i = 0; i < 3; ++i)
        speed.addItem (40 + i, speedNames[i], true, s.analyzer.speed == i);

    const char* avgNames[] = { "Off", "Short", "Medium", "Long" };
    for (int i = 0; i < 4; ++i)
        averaging.addItem (50 + i, avgNames[i], true, s.analyzer.averaging == i);

    const float decays[] = { 6.0f, 12.0f, 24.0f, 48.0f, 96.0f };
    for (int i = 0; i < 5; ++i)
        decay.addItem (60 + i, juce::String ((int) decays[i]) + " dB/s", true, std::abs (s.analyzer.decayDbPerSecond - decays[i]) < 0.01f);

    const float slopes[] = { 0.0f, 3.0f, 4.5f, 6.0f };
    for (int i = 0; i < 4; ++i)
        slope.addItem (70 + i, juce::String (slopes[i], 1) + " dB/oct", true, std::abs (s.analyzer.slopeDbPerOctave - slopes[i]) < 0.01f);

    const float ranges[] = { 6.0f, 12.0f, 18.0f, 30.0f };
    for (int i = 0; i < 4; ++i)
        range.addItem (80 + i, juce::String::charToString (0xb1) + juce::String ((int) ranges[i]) + " dB", true, std::abs (s.displayRangeDb - ranges[i]) < 0.01f);

    const int sizes[][2] = { { 900, 560 }, { 1120, 680 }, { 1400, 860 }, { 1760, 1060 } };
    const char* sizeNames[] = { "Small", "Medium", "Large", "Extra large" };
    for (int i = 0; i < 4; ++i)
        size.addItem (90 + i, juce::String (sizeNames[i]) + "  (" + juce::String (sizes[i][0]) + " x " + juce::String (sizes[i][1]) + ")");

    menu.addSubMenu ("Mode", mode);
    menu.addSubMenu ("Resolution", resolution);
    menu.addSubMenu ("Speed", speed);
    menu.addSubMenu ("Averaging", averaging);
    menu.addSubMenu ("Peak decay", decay);
    menu.addSubMenu ("Display slope", slope);
    menu.addSectionHeader ("Display");
    menu.addSubMenu ("EQ scale", range);
    menu.addSubMenu ("Window size", size);

    juce::Component::SafePointer<TopBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
                        [safe, s, decays, slopes, ranges, sizes] (int r) mutable
                        {
                            if (safe == nullptr || r == 0)
                                return;

                            if (r >= 90 && r < 94)
                            {
                                if (safe->onWindowSizeRequested)
                                    safe->onWindowSizeRequested (sizes[r - 90][0], sizes[r - 90][1]);
                                return;
                            }

                            auto n = s;
                            if (r == 1) n.showPreSpectrum = ! n.showPreSpectrum;
                            else if (r == 2) n.showPostSpectrum = ! n.showPostSpectrum;
                            else if (r == 3) n.analyzer.freeze = ! n.analyzer.freeze;
                            else if (r == 10) n.analyzer.mode = Mode::RMS;
                            else if (r == 11) n.analyzer.mode = Mode::Peak;
                            else if (r >= 31 && r <= 35) n.analyzer.fftOrder = r - 20;
                            else if (r >= 40 && r < 43) n.analyzer.speed = r - 40;
                            else if (r >= 50 && r < 54) n.analyzer.averaging = r - 50;
                            else if (r >= 60 && r < 65) n.analyzer.decayDbPerSecond = decays[r - 60];
                            else if (r >= 70 && r < 74) n.analyzer.slopeDbPerOctave = slopes[r - 70];
                            else if (r >= 80 && r < 84) n.displayRangeDb = ranges[r - 80];

                            safe->ctx.processor.setUIState (n);
                            if (safe->onUIStateChanged)
                                safe->onUIStateChanged (n);
                        });
}

} // namespace eyra::ui
