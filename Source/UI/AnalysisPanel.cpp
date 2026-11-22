#include "AnalysisPanel.h"

#include "EyraLookAndFeel.h"

namespace eyra::ui
{

using analysis::AnalysisController;
using analysis::AnalysisResult;

juce::Colour colourForSeverity (analysis::Severity s)
{
    switch (s)
    {
        case analysis::Severity::Significant: return colours::danger;
        case analysis::Severity::Moderate:    return colours::warning;
        case analysis::Severity::Minor:       break;
    }
    return colours::textDim;
}

namespace
{
    constexpr int kRowPadding = 8;

    juce::TextLayout detailLayout (const juce::String& text, float width)
    {
        juce::AttributedString as;
        as.append (text, uiFont (11.5f), colours::textDim);
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (as, width);
        return layout;
    }

    juce::String severityName (analysis::Severity s)
    {
        switch (s)
        {
            case analysis::Severity::Significant: return "Significant";
            case analysis::Severity::Moderate:    return "Moderate";
            case analysis::Severity::Minor:       break;
        }
        return "Minor";
    }
} // namespace

//==============================================================================
SuggestionList::SuggestionList (EditorContext& context) : ctx (context) {}

void SuggestionList::setSuggestions (std::shared_ptr<const AnalysisResult> r, std::vector<correction::Suggestion> s)
{
    result = std::move (r);
    suggestions = std::move (s);
    checked.clear();
    for (const auto& sg : suggestions)
        checked.push_back (sg.band.has_value()); // left-alone moves can be chosen, but start unticked
    hovered = -1;
    repaint();
}

std::vector<dsp::BandParams> SuggestionList::checkedBands() const
{
    std::vector<dsp::BandParams> bands;
    for (size_t i = 0; i < suggestions.size(); ++i)
        if (checked[i])
        {
            if (suggestions[i].band)
                bands.push_back (*suggestions[i].band);
            else if (suggestions[i].optionalBand)
                bands.push_back (*suggestions[i].optionalBand);
        }
    return bands;
}

int SuggestionList::numChecked() const { return (int) checkedBands().size(); }

bool SuggestionList::allChecked() const
{
    for (size_t i = 0; i < suggestions.size(); ++i)
        if ((suggestions[i].band || suggestions[i].optionalBand) && ! checked[i])
            return false;
    return true;
}

void SuggestionList::setAllChecked (bool shouldBeChecked)
{
    for (size_t i = 0; i < suggestions.size(); ++i)
        if (suggestions[i].band || suggestions[i].optionalBand)
            checked[i] = shouldBeChecked;
    repaint();
    if (onCheckedChanged)
        onCheckedChanged();
}

juce::String SuggestionList::headingFor (int index) const
{
    const auto& sg = suggestions[(size_t) index];
    if (result != nullptr && sg.findingIndex >= 0 && sg.findingIndex < (int) result->findings.size())
        return result->findings[(size_t) sg.findingIndex].title;
    if (sg.heading.isNotEmpty())
        return sg.heading;
    switch (sg.kind)
    {
        case correction::Suggestion::Kind::HighPass:    return "High-pass";
        case correction::Suggestion::Kind::Enhancement: return "Polish";
        case correction::Suggestion::Kind::Cut:
        case correction::Suggestion::Kind::LeftAlone:   break;
    }
    return "Correction";
}

juce::String SuggestionList::detailFor (int index) const
{
    const auto& sg = suggestions[(size_t) index];
    juce::String d;
    if (sg.kind != correction::Suggestion::Kind::LeftAlone && result != nullptr && sg.findingIndex >= 0
        && sg.findingIndex < (int) result->findings.size())
        d = result->findings[(size_t) sg.findingIndex].detail + "\n";
    return d + sg.reason;
}

int SuggestionList::rowHeight (int index, int width) const
{
    return 38 + (int) std::ceil (detailLayout (detailFor (index), (float) (width - 22)).getHeight()) + kRowPadding;
}

int SuggestionList::getIdealHeight (int width) const
{
    int h = 0;
    for (int i = 0; i < (int) suggestions.size(); ++i)
        h += rowHeight (i, width);
    return h;
}

int SuggestionList::rowAt (int y) const
{
    int top = 0;
    for (int i = 0; i < (int) suggestions.size(); ++i)
    {
        const int h = rowHeight (i, getWidth());
        if (y >= top && y < top + h)
            return i;
        top += h;
    }
    return -1;
}

void SuggestionList::paint (juce::Graphics& g)
{
    using K = correction::Suggestion::Kind;
    int top = 0;
    for (int i = 0; i < (int) suggestions.size(); ++i)
    {
        const auto& sg = suggestions[(size_t) i];
        const auto* finding = (result != nullptr && sg.findingIndex >= 0 && sg.findingIndex < (int) result->findings.size())
                                  ? &result->findings[(size_t) sg.findingIndex] : nullptr;
        const int h = rowHeight (i, getWidth());
        auto row = juce::Rectangle<int> (0, top, getWidth(), h);
        const bool selected = finding != nullptr && ctx.selectedFinding == sg.findingIndex;

        if (selected || hovered == i)
        {
            g.setColour (selected ? colours::accent.withAlpha (0.12f) : colours::control.withAlpha (0.6f));
            g.fillRoundedRectangle (row.reduced (2, 1).toFloat(), 4.0f);
        }

        const auto colour = finding != nullptr ? colourForSeverity (finding->severityLabel()) : colours::accent;
        g.setColour (colour);
        g.fillRoundedRectangle ((float) row.getX() + 6.0f, (float) top + 7.0f, 3.0f, (float) h - 14.0f, 1.5f);

        auto content = row.withTrimmedLeft (16).withTrimmedRight (6).withTrimmedTop (5);
        auto titleRow = content.removeFromTop (16);
        if (finding != nullptr)
        {
            g.setFont (uiFont (10.5f));
            g.setColour (colour);
            const juce::String tag = severityName (finding->severityLabel()) + "  " + juce::String (juce::roundToInt (100.0 * finding->confidence)) + "%";
            const int tagW = juce::GlyphArrangement::getStringWidthInt (uiFont (10.5f), tag) + 4;
            g.drawText (tag, titleRow.removeFromRight (tagW), juce::Justification::centredRight, false);
        }
        g.setFont (uiFont (13.0f, true));
        g.setColour (colours::text);
        g.drawFittedText (headingFor (i), titleRow, juce::Justification::centredLeft, 1);

        // The proposed move, with a tick box when there is one.
        auto actionRow = content.removeFromTop (18);
        const auto* bandToShow = sg.band ? &*sg.band : (sg.optionalBand ? &*sg.optionalBand : nullptr);
        if (bandToShow != nullptr)
        {
            auto box = actionRow.removeFromLeft (14).withSizeKeepingCentre (12, 12).toFloat();
            g.setColour (checked[(size_t) i] ? colours::accent : colours::outline);
            g.drawRoundedRectangle (box, 2.5f, 1.2f);
            if (checked[(size_t) i])
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 2.5f, box.getCentreY());
                tick.lineTo (box.getX() + 5.0f, box.getBottom() - 3.0f);
                tick.lineTo (box.getRight() - 2.5f, box.getY() + 3.0f);
                g.strokePath (tick, juce::PathStrokeType (1.6f));
            }
            actionRow.removeFromLeft (5);
        }
        g.setFont (uiFont (12.0f, true));
        g.setColour (sg.kind == K::LeftAlone ? colours::textFaint : (sg.kind == K::Enhancement ? colours::accent.brighter (0.3f) : colours::accent));
        const bool optionalTicked = ! sg.band && sg.optionalBand && checked[(size_t) i];
        if (! sg.band && sg.optionalBand)
            g.setColour (optionalTicked ? colours::accent : colours::textDim);
        g.drawFittedText (sg.band ? sg.title
                                  : (sg.optionalBand ? "Optional: " + correction::describeBand (*sg.optionalBand)
                                                     : "No EQ: " + sg.title.toLowerCase()),
                          actionRow, juce::Justification::centredLeft, 1);

        content.removeFromTop (1);
        detailLayout (detailFor (i), (float) content.getWidth()).draw (g, content.toFloat());

        g.setColour (colours::outline.withAlpha (0.5f));
        g.drawHorizontalLine (top + h - 1, 10.0f, (float) getWidth() - 6.0f);
        top += h;
    }
}

void SuggestionList::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r != hovered)
    {
        hovered = r;
        repaint();
    }
}

void SuggestionList::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SuggestionList::mouseDown (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r < 0)
        return;
    const auto& sg = suggestions[(size_t) r];

    // Tick box: the action line, left part.
    int top = 0;
    for (int i = 0; i < r; ++i)
        top += rowHeight (i, getWidth());
    if ((sg.band || sg.optionalBand) && e.y >= top + 21 && e.y < top + 39 && e.x < 40)
    {
        checked[(size_t) r] = ! checked[(size_t) r];
        repaint();
        if (onCheckedChanged)
            onCheckedChanged();
        return;
    }

    if (sg.findingIndex >= 0)
    {
        ctx.selectedFinding = (sg.findingIndex == ctx.selectedFinding) ? -1 : sg.findingIndex;
        repaint();
        if (onSelectionChanged)
            onSelectionChanged();
    }
}

//==============================================================================
AnalysisPanel::AnalysisPanel (EditorContext& context) : ctx (context), list (context)
{
    isStandalone = ctx.processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    const auto settings = ctx.processor.getCorrectionSettings();

    for (auto* c : std::initializer_list<juce::Component*> { &primaryButton, &secondaryButton, &instrumentBox, &modeBox, &strengthBox,
                                                              &amountSlider, &countSlider, &preserveToggle, &highPassToggle,
                                                              &autoApplyToggle, &applyButton, &resetButton, &selectAllButton, &viewport })
        addAndMakeVisible (c);
    addChildComponent (fileButton);
    fileButton.setVisible (isStandalone);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);

    // Instrument: chosen by the user, grouped by family.
    instrumentBox.addItem (correction::instrumentName (correction::Instrument::General), 1);
    for (const auto& group : correction::instrumentGroups())
    {
        instrumentBox.addSectionHeading (group);
        for (int i = 1; i < (int) correction::Instrument::NumInstruments; ++i)
            if (correction::instrumentGroup ((correction::Instrument) i) == group)
                instrumentBox.addItem (correction::instrumentName ((correction::Instrument) i), i + 1);
    }
    instrumentBox.setSelectedId ((int) settings.instrument + 1, juce::dontSendNotification);
    instrumentBox.setTooltip ("The instrument on this track. Corrections respect the parts of the spectrum that make up its sound.");

    modeBox.addItemList (correction::modeNames(), 1);
    modeBox.setSelectedId ((int) settings.mode + 1, juce::dontSendNotification);
    modeBox.setTooltip ("Transparent: only removes problems. Polished: also adds subtle enhancement where the recording lacks it.");
    strengthBox.addItemList (correction::strengthNames(), 1);
    strengthBox.setSelectedId ((int) settings.strength + 1, juce::dontSendNotification);
    strengthBox.setTooltip ("How much of each measured problem is removed, and how minor a problem may be to get a correction.");

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setRange (0.0, 150.0, 1.0);
    amountSlider.setTextValueSuffix (" %");
    amountSlider.setValue (settings.amount * 100.0, juce::dontSendNotification);
    amountSlider.setTooltip ("Scales every correction (100% = as proposed).");
    countSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    countSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    countSlider.setRange (1.0, 12.0, 1.0);
    countSlider.setValue (settings.maxCorrections, juce::dontSendNotification);
    countSlider.setTooltip ("Most EQ bands the correction may use; the most important problems come first.");

    preserveToggle.setToggleState (settings.preserveCharacter, juce::dontSendNotification);
    preserveToggle.setTooltip ("Treat the instrument's characteristic regions with care: smaller cuts, and none for mild excess.");
    highPassToggle.setToggleState (settings.autoHighPass, juce::dontSendNotification);
    highPassToggle.setTooltip ("Add a high-pass suited to the instrument, kept below the lowest measured content. (Rumble always gets one.)");
    autoApplyToggle.setToggleState (settings.applyAutomatically, juce::dontSendNotification);
    autoApplyToggle.setTooltip ("On: corrections become EQ bands as soon as an analysis finishes. Off: review and tick them first.");

    for (auto* box : { &instrumentBox, &modeBox, &strengthBox })
        box->onChange = [this] { settingsChanged(); };
    for (auto* slider : { &amountSlider, &countSlider })
    {
        slider->onValueChange = [this] { settingsChanged(); repaint(); };
    }
    for (auto* toggle : { &preserveToggle, &highPassToggle, &autoApplyToggle })
        toggle->onClick = [this] { settingsChanged(); };

    for (auto* b : { &primaryButton, &applyButton })
    {
        b->setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.2f));
        b->setColour (juce::TextButton::textColourOffId, colours::accent);
    }
    primaryButton.setTooltip ("Listen to the playing audio and analyse it for frequency problems");
    fileButton.setTooltip ("Analyse an audio file (WAV, AIFF, FLAC, Ogg, MP3)");
    applyButton.setTooltip ("Add the ticked corrections as EQ bands (undoable). They replace corrections applied before.");
    resetButton.setTooltip ("Remove the EQ bands added by Apply corrections (undoable)");

    auto& controller = ctx.processor.analysis;
    primaryButton.onClick = [&controller]
    {
        if (controller.getState() == AnalysisController::State::Listening)
            controller.stopAndAnalyse();
        else
            controller.startListening();
    };
    secondaryButton.onClick = [&controller]
    {
        if (controller.getState() == AnalysisController::State::Listening)
            controller.cancel();
        else
            controller.clearResult();
    };
    fileButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file to analyse", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      ctx.processor.analysis.analyseFile (file);
                              });
    };
    applyButton.onClick = [this] { applyChecked(); };
    selectAllButton.setTooltip ("Tick every proposed move, including the ones EyraEQ would leave alone (or untick all)");
    selectAllButton.onClick = [this] { list.setAllChecked (! list.allChecked()); };
    resetButton.onClick = [this]
    {
        ctx.processor.resetCorrections();
        applyMessage = "Corrections removed.";
        repaint();
    };

    list.onSelectionChanged = [this]
    {
        if (auto* parent = getParentComponent())
            parent->repaint();
    };
    list.onCheckedChanged = [this] { refresh(); };

    refresh();
}

void AnalysisPanel::settingsChanged()
{
    correction::Settings s;
    s.instrument = (correction::Instrument) juce::jlimit (0, (int) correction::Instrument::NumInstruments - 1, instrumentBox.getSelectedId() - 1);
    s.mode = (correction::Mode) juce::jlimit (0, 1, modeBox.getSelectedId() - 1);
    s.strength = (correction::Strength) juce::jlimit (0, 2, strengthBox.getSelectedId() - 1);
    s.amount = amountSlider.getValue() / 100.0;
    s.maxCorrections = (int) countSlider.getValue();
    s.preserveCharacter = preserveToggle.getToggleState();
    s.autoHighPass = highPassToggle.getToggleState();
    s.applyAutomatically = autoApplyToggle.getToggleState();
    ctx.processor.setCorrectionSettings (s);
    regenerate();
}

void AnalysisPanel::regenerate()
{
    list.setSuggestions (ctx.analysis, ctx.processor.getSuggestions());
    layoutList();
    refresh();
}

void AnalysisPanel::applyChecked()
{
    const auto bands = list.checkedBands();
    const int placed = ctx.processor.applyCorrections (bands);
    if (placed < (int) bands.size())
        applyMessage = "Applied " + juce::String (placed) + " of " + juce::String ((int) bands.size())
                     + " corrections: no free EQ bands left. Remove some bands and apply again.";
    else
        applyMessage = "Applied " + juce::String (placed) + (placed == 1 ? " correction" : " corrections")
                     + " as EQ bands. Adjust, bypass or delete them like any band.";
    repaint();
}

void AnalysisPanel::refresh()
{
    auto& controller = ctx.processor.analysis;
    const auto state = controller.getState();
    const int version = controller.getResultVersion();

    if (version != shownVersion)
    {
        shownVersion = version;
        ctx.analysis = controller.getResult();
        ctx.selectedFinding = -1;
        applyMessage.clear();
        list.setSuggestions (ctx.analysis, ctx.processor.getSuggestions());
        layoutList();
        if (ctx.analysis != nullptr && ctx.processor.getCorrectionSettings().applyAutomatically && list.numChecked() > 0)
            applyChecked();
        repaint();
    }

    const bool busy = state == AnalysisController::State::Analysing;
    const bool listening = state == AnalysisController::State::Listening;

    primaryButton.setButtonText (listening ? "Stop & analyze" : (busy ? "Analyzing..." : (ctx.analysis != nullptr ? "Reanalyze" : "Analyze")));
    primaryButton.setEnabled (listening ? controller.canStopAndAnalyse() : ! busy);
    secondaryButton.setButtonText (listening ? "Cancel" : "Clear");
    secondaryButton.setEnabled (listening || (! busy && ctx.analysis != nullptr));
    fileButton.setEnabled (! busy && ! listening);

    bool applied = false;
    for (int slot : ctx.processor.getCorrectionSlots())
        applied |= ctx.readBand (slot).used;
    const int n = list.numChecked();
    applyButton.setButtonText ((applied ? "Update EQ" : "Apply to EQ") + juce::String (n > 0 ? " (" + juce::String (n) + ")" : ""));
    applyButton.setEnabled (n > 0 && ! busy && ! listening);
    resetButton.setEnabled (applied);
    selectAllButton.setButtonText (list.allChecked() && list.numChecked() > 0 ? "Select none" : "Select all");
    selectAllButton.setEnabled (! list.getSuggestions().empty());

    if (state != shownState || listening || busy)
    {
        shownState = state;
        repaint();
    }
}

juce::String AnalysisPanel::summaryText() const
{
    const auto& r = *ctx.analysis;
    juce::String s;
    s << juce::String (r.activeSeconds, 1) << " s of signal analysed, confidence " << juce::roundToInt (100.0 * r.confidence) << "%\n";
    if (r.pitch.pitched)
        s << "Pitched: " << analysis::noteName (r.pitch.medianHz) << " (" << analysis::describeFrequency (r.pitch.medianHz) << ")"
          << (r.pitch.spreadCents > 100.0 ? ", varying" : "") << "\n";
    else
        s << "No steady pitch\n";
    s << (r.percussive ? "Percussive" : "Sustained") << ", tilt " << juce::String (r.spectralTiltDbPerOct, 1) << " dB/oct";
    return s;
}

void AnalysisPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("AUDIO ANALYSIS", getLocalBounds().reduced (12, 0).removeFromTop (30), juce::Justification::centredLeft, false);
    g.drawText ("INSTRUMENT", instrumentLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("CORRECTION", correctionHeaderArea, juce::Justification::centredLeft, false);
    g.setFont (uiFont (11.5f));
    g.drawText ("Amount " + juce::String ((int) amountSlider.getValue()) + "%", amountLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("Max bands " + juce::String ((int) countSlider.getValue()), countLabelArea, juce::Justification::centredLeft, false);

    auto& controller = ctx.processor.analysis;
    const auto state = controller.getState();
    auto area = statusArea;

    auto drawLines = [&g] (juce::Rectangle<int> r, const juce::String& text, juce::Colour c, float size = 12.0f, int lines = 4)
    {
        g.setColour (c);
        g.setFont (uiFont (size));
        g.drawFittedText (text, r, juce::Justification::topLeft, lines, 1.0f);
    };

    if (state == AnalysisController::State::Listening)
    {
        const double active = controller.getActiveSeconds();
        const double target = AnalysisController::kTargetActiveSeconds;
        g.setColour (colours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText ("Listening to the input", area.removeFromTop (18), juce::Justification::centredLeft, false);
        area.removeFromTop (4);
        auto bar = area.removeFromTop (6).toFloat();
        g.setColour (colours::control);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, active / target)), 3.0f);
        area.removeFromTop (4);
        juce::String text = juce::String (active, 1) + " s of signal heard (stops at " + juce::String ((int) target) + " s).";
        if (! controller.isSignalPresent())
            drawLines (area, text + " Waiting for audio: start playback in your host.", colours::warning);
        else
            drawLines (area, text + " Loop the part that matters.", colours::textDim);
    }
    else if (state == AnalysisController::State::Analysing)
    {
        drawLines (area, controller.getStatusMessage(), colours::text, 13.0f);
    }
    else if (applyMessage.isNotEmpty())
    {
        drawLines (area, applyMessage, colours::accent, 12.0f);
    }
    else if (ctx.analysis != nullptr)
    {
        const auto& r = *ctx.analysis;
        g.setColour (colours::text);
        g.setFont (uiFont (12.0f, true));
        g.drawFittedText (controller.getSourceDescription(), area.removeFromTop (16), juce::Justification::centredLeft, 1);
        area.removeFromTop (2);
        if (r.status == AnalysisResult::Status::Ok)
            drawLines (area, r.findings.empty() ? juce::String ("No frequency problems found. Nothing to correct.") : summaryText(),
                       r.findings.empty() ? colours::accent : colours::textDim, 11.5f);
        else
            drawLines (area, r.message, colours::warning);
    }
    else
    {
        drawLines (area, "Choose the instrument, play the track (loop the part that matters), then press Analyze.", colours::textDim);
    }

    g.setColour (colours::outline);
    g.drawHorizontalLine (footerArea.getY(), 10.0f, (float) getWidth() - 10.0f);
    drawLines (footerArea.reduced (12, 5), "Corrections are measured from the audio and become normal EQ bands. Trust your ears; everything is undoable.",
               colours::textFaint, 11.0f, 2);
}

void AnalysisPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromTop (30);

    auto instrumentRow = area.removeFromTop (24);
    instrumentLabelArea = instrumentRow.removeFromLeft (84);
    instrumentBox.setBounds (instrumentRow);
    area.removeFromTop (8);

    auto buttons = area.removeFromTop (28);
    secondaryButton.setBounds (buttons.removeFromRight (70));
    buttons.removeFromRight (6);
    primaryButton.setBounds (buttons);
    if (isStandalone)
    {
        area.removeFromTop (6);
        fileButton.setBounds (area.removeFromTop (24));
    }

    area.removeFromTop (6);
    statusArea = area.removeFromTop (58);
    area.removeFromTop (4);

    correctionHeaderArea = area.removeFromTop (20);
    auto row = area.removeFromTop (24);
    modeBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
    row.removeFromLeft (6);
    strengthBox.setBounds (row);
    area.removeFromTop (4);
    row = area.removeFromTop (22);
    amountLabelArea = row.removeFromLeft (92);
    amountSlider.setBounds (row);
    row = area.removeFromTop (22);
    countLabelArea = row.removeFromLeft (92);
    countSlider.setBounds (row);
    row = area.removeFromTop (22);
    preserveToggle.setBounds (row.removeFromLeft (row.getWidth() / 2 + 14));
    highPassToggle.setBounds (row);
    autoApplyToggle.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);
    row = area.removeFromTop (28);
    resetButton.setBounds (row.removeFromRight (56));
    row.removeFromRight (4);
    selectAllButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (4);
    applyButton.setBounds (row);
    area.removeFromTop (6);

    footerArea = getLocalBounds().removeFromBottom (40);
    area.setBottom (footerArea.getY() - 4);
    viewport.setBounds (area.withX (4).withWidth (getWidth() - 6));
    layoutList();
}

void AnalysisPanel::layoutList()
{
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    list.setSize (std::max (10, w), std::max (viewport.getHeight(), list.getIdealHeight (w)));
}

} // namespace eyra::ui
