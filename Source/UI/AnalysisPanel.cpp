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
FindingsList::FindingsList (EditorContext& context) : ctx (context) {}

void FindingsList::setFindings (std::shared_ptr<const AnalysisResult> r)
{
    result = std::move (r);
    hovered = -1;
    repaint();
}

int FindingsList::rowHeight (int index, int width) const
{
    const auto& f = result->findings[(size_t) index];
    return 22 + (int) std::ceil (detailLayout (f.detail, (float) (width - 22)).getHeight()) + kRowPadding;
}

int FindingsList::getIdealHeight (int width) const
{
    if (result == nullptr)
        return 0;
    int h = 0;
    for (int i = 0; i < (int) result->findings.size(); ++i)
        h += rowHeight (i, width);
    return h;
}

int FindingsList::rowAt (int y) const
{
    if (result == nullptr)
        return -1;
    int top = 0;
    for (int i = 0; i < (int) result->findings.size(); ++i)
    {
        const int h = rowHeight (i, getWidth());
        if (y >= top && y < top + h)
            return i;
        top += h;
    }
    return -1;
}

void FindingsList::paint (juce::Graphics& g)
{
    if (result == nullptr)
        return;

    int top = 0;
    for (int i = 0; i < (int) result->findings.size(); ++i)
    {
        const auto& f = result->findings[(size_t) i];
        const int h = rowHeight (i, getWidth());
        auto row = juce::Rectangle<int> (0, top, getWidth(), h);
        const bool selected = ctx.selectedFinding == i;

        if (selected || hovered == i)
        {
            g.setColour (selected ? colours::accent.withAlpha (0.12f) : colours::control.withAlpha (0.6f));
            g.fillRoundedRectangle (row.reduced (2, 1).toFloat(), 4.0f);
        }

        const auto colour = colourForSeverity (f.severityLabel());
        g.setColour (colour);
        g.fillRoundedRectangle ((float) row.getX() + 6.0f, (float) top + 7.0f, 3.0f, (float) h - 14.0f, 1.5f);

        auto content = row.withTrimmedLeft (16).withTrimmedRight (6).withTrimmedTop (5);
        auto titleRow = content.removeFromTop (16);
        g.setFont (uiFont (10.5f));
        g.setColour (colour);
        const juce::String tag = severityName (f.severityLabel()) + "  " + juce::String (juce::roundToInt (100.0 * f.confidence)) + "%";
        const int tagW = juce::GlyphArrangement::getStringWidthInt (uiFont (10.5f), tag) + 4;
        g.drawText (tag, titleRow.removeFromRight (tagW), juce::Justification::centredRight, false);

        g.setFont (uiFont (13.0f, true));
        g.setColour (colours::text);
        g.drawFittedText (f.title, titleRow, juce::Justification::centredLeft, 1);

        content.removeFromTop (1);
        detailLayout (f.detail, (float) content.getWidth()).draw (g, content.toFloat());

        g.setColour (colours::outline.withAlpha (0.5f));
        g.drawHorizontalLine (top + h - 1, 10.0f, (float) getWidth() - 6.0f);
        top += h;
    }
}

void FindingsList::mouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r != hovered)
    {
        hovered = r;
        repaint();
    }
}

void FindingsList::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void FindingsList::mouseDown (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    ctx.selectedFinding = (r == ctx.selectedFinding) ? -1 : r;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

//==============================================================================
AnalysisPanel::AnalysisPanel (EditorContext& context) : ctx (context), list (context)
{
    isStandalone = ctx.processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;

    addAndMakeVisible (primaryButton);
    addAndMakeVisible (secondaryButton);
    addChildComponent (fileButton);
    fileButton.setVisible (isStandalone);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);

    primaryButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.2f));
    primaryButton.setColour (juce::TextButton::textColourOffId, colours::accent);
    primaryButton.setTooltip ("Listen to the playing audio and analyse it for frequency problems");
    fileButton.setTooltip ("Analyse an audio file (WAV, AIFF, FLAC, Ogg, MP3)");

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

    list.onSelectionChanged = [this]
    {
        if (auto* parent = getParentComponent())
            parent->repaint();
    };

    refresh();
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
        list.setFindings (ctx.analysis);
        layoutList();
    }

    const bool busy = state == AnalysisController::State::Analysing;
    const bool listening = state == AnalysisController::State::Listening;

    primaryButton.setButtonText (listening ? "Stop & analyze" : (busy ? "Analyzing..." : "Analyze"));
    primaryButton.setEnabled (listening ? controller.canStopAndAnalyse() : ! busy);
    secondaryButton.setButtonText (listening ? "Cancel" : "Clear");
    secondaryButton.setEnabled (listening || (! busy && ctx.analysis != nullptr));
    fileButton.setEnabled (! busy && ! listening);

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

    auto& controller = ctx.processor.analysis;
    const auto state = controller.getState();
    auto area = statusArea;

    auto drawLines = [&g] (juce::Rectangle<int> r, const juce::String& text, juce::Colour c, float size = 12.5f)
    {
        g.setColour (c);
        g.setFont (uiFont (size));
        g.drawFittedText (text, r, juce::Justification::topLeft, 6, 1.0f);
    };

    if (state == AnalysisController::State::Listening)
    {
        const double active = controller.getActiveSeconds();
        const double target = AnalysisController::kTargetActiveSeconds;
        g.setColour (colours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText ("Listening to the input", area.removeFromTop (18), juce::Justification::centredLeft, false);
        area.removeFromTop (6);

        auto bar = area.removeFromTop (6).toFloat();
        g.setColour (colours::control);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, active / target)), 3.0f);
        area.removeFromTop (6);

        juce::String text = juce::String (active, 1) + " s of signal heard (stops at " + juce::String ((int) target) + " s).";
        if (! controller.isSignalPresent())
            drawLines (area, text + "\nWaiting for audio: start playback in your host.", colours::warning);
        else
            drawLines (area, text + "\nLoop the section that matters; more audio gives more reliable results.", colours::textDim);
    }
    else if (state == AnalysisController::State::Analysing)
    {
        drawLines (area, controller.getStatusMessage(), colours::text, 13.0f);
    }
    else if (ctx.analysis != nullptr)
    {
        const auto& r = *ctx.analysis;
        const auto source = controller.getSourceDescription();
        g.setColour (colours::text);
        g.setFont (uiFont (12.0f, true));
        g.drawFittedText (source, area.removeFromTop (16), juce::Justification::centredLeft, 1);
        area.removeFromTop (4);

        if (r.status == AnalysisResult::Status::Ok)
        {
            drawLines (area.removeFromTop (48), summaryText(), colours::textDim, 12.0f);
            if (r.findings.empty())
                drawLines (area, "No frequency problems found.", colours::accent, 12.5f);
        }
        else
            drawLines (area, r.message, colours::warning);
    }
    else
    {
        drawLines (area, "Play the track (loop the part that matters), then press Analyze. EyraEQ listens to the input "
                         "and looks for rumble, hum, build-ups, resonances and ringing.",
                   colours::textDim);
    }

    g.setColour (colours::outline);
    g.drawHorizontalLine (footerArea.getY(), 10.0f, (float) getWidth() - 10.0f);
    drawLines (footerArea.reduced (12, 6), "Instrument recognition is not available yet: findings come from the audio alone. "
                                           "Treat them as suggestions and trust your ears.", colours::textFaint, 11.0f);
}

void AnalysisPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromTop (30);

    auto buttons = area.removeFromTop (28);
    secondaryButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (6);
    primaryButton.setBounds (buttons);

    if (isStandalone)
    {
        area.removeFromTop (6);
        fileButton.setBounds (area.removeFromTop (24));
    }

    area.removeFromTop (10);
    statusArea = area.removeFromTop (92);

    footerArea = getLocalBounds().removeFromBottom (48);
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
