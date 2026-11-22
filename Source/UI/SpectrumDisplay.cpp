#include "SpectrumDisplay.h"

#include "AnalysisPanel.h"
#include "EyraLookAndFeel.h"

#include <cmath>

namespace eyra::ui
{

namespace
{
    constexpr double kMinHz = analysis::SpectrumAnalyzer::kMinDisplayHz;
    constexpr double kMaxHz = analysis::SpectrumAnalyzer::kMaxDisplayHz;
    constexpr float kAnalyzerTopDb = 6.0f;
    constexpr float kAnalyzerBottomDb = -102.0f;
    constexpr float kNodeRadius = 8.5f;
    constexpr float kRightMargin = 34.0f;
    constexpr float kBottomMargin = 20.0f;

    juce::String formatHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
        return (hz < 100.0 ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz))) + " Hz";
    }

    juce::String formatGridHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String ((int) (hz / 1000.0)) + "k";
        return juce::String ((int) hz);
    }

    juce::String formatGain (double db)
    {
        return (db > 0.0 ? "+" : "") + juce::String (db, 1) + " dB";
    }
} // namespace

SpectrumDisplay::SpectrumDisplay (EditorContext& context)
    : ctx (context), preAnalyzer (context.processor.preFifo), postAnalyzer (context.processor.postFifo)
{
    setOpaque (true);
    applyUIState (ctx.processor.getUIState());
}

void SpectrumDisplay::applyUIState (const EyraUIState& s)
{
    preAnalyzer.setSettings (s.analyzer);
    postAnalyzer.setSettings (s.analyzer);
    showPre = s.showPreSpectrum;
    showPost = s.showPostSpectrum;
    showAnalysis = s.showAnalysisOverlay;
    rangeDb = s.displayRangeDb;
    repaint();
}

//==============================================================================
float SpectrumDisplay::xForFrequency (double hz) const noexcept
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + (float) t * plot.getWidth();
}

double SpectrumDisplay::frequencyForX (float x) const noexcept
{
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / plot.getWidth()));
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float SpectrumDisplay::yForGain (double db) const noexcept
{
    return plot.getCentreY() - (float) (db / rangeDb) * plot.getHeight() * 0.5f;
}

double SpectrumDisplay::gainForY (float y) const noexcept
{
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * rangeDb;
}

float SpectrumDisplay::yForAnalyzerDb (float db) const noexcept
{
    const float t = (kAnalyzerTopDb - db) / (kAnalyzerTopDb - kAnalyzerBottomDb);
    return plot.getY() + juce::jlimit (0.0f, 1.0f, t) * plot.getHeight();
}

//==============================================================================
void SpectrumDisplay::resized()
{
    plot = getLocalBounds().toFloat().withTrimmedRight (kRightMargin).withTrimmedBottom (kBottomMargin).reduced (1.0f, 8.0f);

    const int numPoints = juce::jmax (200, (int) plot.getWidth() / 2);
    curveFrequencies.resize ((size_t) numPoints);
    for (int i = 0; i < numPoints; ++i)
        curveFrequencies[(size_t) i] = kMinHz * std::pow (kMaxHz / kMinHz, (double) i / (numPoints - 1));

    refreshCurves (true);
}

void SpectrumDisplay::refreshCurves (bool force)
{
    const double sr = ctx.processor.getEngineSampleRate();
    if (! juce::exactlyEqual (sr, cachedSampleRate))
    {
        cachedSampleRate = sr;
        force = true;
    }

    const size_t n = curveFrequencies.size();
    bool changed = force;

    for (int b = 0; b < params::kNumBands; ++b)
    {
        auto& c = cache[(size_t) b];
        const auto p = ctx.readBand (b);
        if (c.valid && ! force && p == c.params)
            continue;

        c.params = p;
        c.valid = true;
        changed = true;
        c.curveDb.assign (n, 0.0f);

        if (p.used)
        {
            dsp::FilterSpec spec { p.type, p.frequency, p.gainDb, p.q, p.slopeIndex };
            const auto cascade = dsp::design (spec, sr);
            for (size_t i = 0; i < n; ++i)
                c.curveDb[i] = (float) cascade.magnitudeDb (curveFrequencies[i], sr);
        }
    }

    if (! changed)
        return;

    compositeDb.assign (n, 0.0f);
    for (const auto& c : cache)
        if (c.params.used && ! c.params.bypassed)
            for (size_t i = 0; i < n; ++i)
                compositeDb[i] += c.curveDb[i];

    repaint();
}

void SpectrumDisplay::tick (double elapsedSeconds)
{
    // Hidden analysers still drain their FIFOs so they resume with fresh data.
    bool needsRepaint = false;
    needsRepaint |= preAnalyzer.update (elapsedSeconds) && showPre;
    needsRepaint |= postAnalyzer.update (elapsedSeconds) && showPost;

    refreshCurves (false);

    // Drop selections of bands that were disabled elsewhere (undo, presets, automation).
    for (int i = ctx.selection.getNumSelected(); --i >= 0;)
        if (! cache[(size_t) ctx.selection.getSelectedItem (i)].params.used)
            ctx.selection.deselect (ctx.selection.getSelectedItem (i));

    if (wheelBand >= 0 && juce::Time::getMillisecondCounterHiRes() - lastWheelTime > 350.0)
        endWheelGesture();

    if (needsRepaint)
        repaint();
}

//==============================================================================
juce::Point<float> SpectrumDisplay::nodePosition (int band) const
{
    const auto& p = cache[(size_t) band].params;
    const float x = xForFrequency (p.frequency);
    const float y = dsp::typeUsesGain (p.type) ? yForGain (juce::jlimit ((double) -rangeDb, (double) rangeDb, p.gainDb)) : yForGain (0.0);
    return { x, y };
}

int SpectrumDisplay::hitTestNode (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = kNodeRadius + 3.0f;

    // Iterate in reverse so the node drawn on top wins.
    for (int b = params::kNumBands; --b >= 0;)
    {
        if (! cache[(size_t) b].params.used)
            continue;
        const float d = nodePosition (b).getDistanceFrom (pos);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

//==============================================================================
void SpectrumDisplay::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setGradientFill (juce::ColourGradient (colours::panel.withAlpha (0.6f), plot.getX(), plot.getY(),
                                             colours::background, plot.getX(), plot.getBottom(), false));
    g.fillRect (plot);

    drawGrid (g);

    if (showPre)
        drawSpectrum (g, preAnalyzer.getLevels(), colours::spectrumPre, true);
    if (showPost)
        drawSpectrum (g, postAnalyzer.getLevels(), colours::spectrumPost, false);

    drawAnalysisOverlay (g);

    const size_t n = curveFrequencies.size();
    auto curvePath = [&] (const std::vector<float>& db)
    {
        juce::Path path;
        for (size_t i = 0; i < n; ++i)
        {
            const float x = xForFrequency (curveFrequencies[i]);
            const float y = juce::jlimit (plot.getY() - 40.0f, plot.getBottom() + 40.0f, yForGain (db[i]));
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        return path;
    };

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    // Individual band curves: selected bands filled, hovered band outlined.
    for (int b = 0; b < params::kNumBands; ++b)
    {
        const auto& c = cache[(size_t) b];
        if (! c.params.used)
            continue;

        const bool selected = ctx.selection.isSelected (b);
        const bool hovered = b == hoveredBand;
        if (! selected && ! hovered)
            continue;

        auto colour = colours::forBand (b);
        if (c.params.bypassed)
            colour = colour.withSaturation (0.0f);

        auto path = curvePath (c.curveDb);
        if (selected)
        {
            juce::Path fill (path);
            fill.lineTo (xForFrequency (curveFrequencies.back()), yForGain (0.0));
            fill.lineTo (xForFrequency (curveFrequencies.front()), yForGain (0.0));
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.13f));
            g.fillPath (fill);
        }
        g.setColour (colour.withAlpha (selected ? 0.85f : 0.55f));
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }

    // Composite response
    if (! compositeDb.empty())
    {
        const bool bypassed = ctx.processor.apvts.getRawParameterValue (params::id::masterBypass)->load() >= 0.5f;
        auto path = curvePath (compositeDb);
        g.setColour (colours::curve.withAlpha (bypassed ? 0.25f : 0.1f));
        g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::curve.withAlpha (bypassed ? 0.35f : 0.95f));
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Nodes
    const int solo = ctx.processor.soloBand.load();
    for (int b = 0; b < params::kNumBands; ++b)
    {
        const auto& p = cache[(size_t) b].params;
        if (! p.used)
            continue;

        const auto pos = nodePosition (b);
        const bool selected = ctx.selection.isSelected (b);
        auto colour = colours::forBand (b);
        if (p.bypassed)
            colour = colours::textFaint;

        const float r = kNodeRadius + (b == hoveredBand ? 1.0f : 0.0f);
        auto circle = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos);

        g.setColour (colours::background.withAlpha (0.85f));
        g.fillEllipse (circle);
        if (selected)
        {
            g.setColour (colour);
            g.fillEllipse (circle);
        }
        g.setColour (colour);
        g.drawEllipse (circle, b == solo ? 2.6f : 1.6f);

        g.setColour (selected ? colours::background : colour);
        g.setFont (uiFont (10.5f, true));
        g.drawText (juce::String (b + 1), circle.toNearestInt(), juce::Justification::centred, false);

        if (p.placement != dsp::StereoPlacement::Stereo)
        {
            static const char* const tags[] = { "", "L", "R", "M", "S" };
            g.setColour (colour.withAlpha (0.9f));
            g.setFont (uiFont (10.0f, true));
            g.drawText (tags[(int) p.placement], circle.translated (0.0f, -r - 7.0f).toNearestInt(), juce::Justification::centred, false);
        }
    }

    if (dragMode == DragMode::Lasso && ! lasso.isEmpty())
    {
        g.setColour (colours::accent.withAlpha (0.08f));
        g.fillRect (lasso);
        g.setColour (colours::accent.withAlpha (0.5f));
        g.drawRect (lasso, 1.0f);
    }

    const int readoutBand = dragMode == DragMode::Nodes ? ctx.primaryBand() : hoveredBand;
    if (readoutBand >= 0 && cache[(size_t) readoutBand].params.used)
        drawReadout (g, readoutBand);
    else if (plot.contains (mousePos) && dragMode == DragMode::None)
    {
        // Cursor frequency readout
        const auto text = formatHz (frequencyForX (mousePos.x));
        g.setFont (uiFont (12.0f));
        g.setColour (colours::textDim);
        g.drawText (text, juce::Rectangle<float> (mousePos.x - 40.0f, plot.getBottom() - 18.0f, 80.0f, 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawVerticalLine ((int) mousePos.x, plot.getY(), plot.getBottom() - 20.0f);
    }
}

void SpectrumDisplay::drawGrid (juce::Graphics& g)
{
    g.setFont (uiFont (11.0f));

    static const double gridHz[] = { 20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800, 1000,
                                     2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000 };
    static const double labelHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

    for (double hz : gridHz)
    {
        const bool strong = juce::exactlyEqual (hz, 100.0) || juce::exactlyEqual (hz, 1000.0) || juce::exactlyEqual (hz, 10000.0);
        g.setColour (strong ? colours::gridStrong : colours::grid);
        g.drawVerticalLine (juce::roundToInt (xForFrequency (hz)), plot.getY(), plot.getBottom());
    }

    g.setColour (colours::textFaint);
    for (double hz : labelHz)
    {
        const float x = xForFrequency (hz);
        g.drawText (formatGridHz (hz), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }

    // Gain grid (EQ scale, right-hand labels)
    const float step = rangeDb <= 6.0f ? 2.0f : (rangeDb <= 12.0f ? 3.0f : (rangeDb <= 18.0f ? 6.0f : 10.0f));
    for (float db = -rangeDb; db <= rangeDb + 0.01f; db += step)
    {
        const float y = yForGain (db);
        const bool zero = std::abs (db) < 0.01f;
        g.setColour (zero ? colours::gridStrong.brighter (0.15f) : colours::grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

        if (y > plot.getY() + 4.0f && y < plot.getBottom() - 4.0f)
        {
            g.setColour (zero ? colours::textDim : colours::textFaint);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<float> (plot.getRight() + 3.0f, y - 7.0f, kRightMargin - 5.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
    }
}

void SpectrumDisplay::drawSpectrum (juce::Graphics& g, const std::vector<float>& levels, juce::Colour colour, bool filled)
{
    juce::Path path;
    const int n = (int) levels.size();
    bool started = false;

    for (int i = 0; i < n; ++i)
    {
        const float x = xForFrequency (analysis::SpectrumAnalyzer::getPointFrequency (i));
        const float y = yForAnalyzerDb (levels[(size_t) i]);
        if (! started)
        {
            path.startNewSubPath (x, plot.getBottom());
            path.lineTo (x, y);
            started = true;
        }
        else
            path.lineTo (x, y);
    }

    if (! started)
        return;

    juce::Path fill (path);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (plot.toNearestInt());

    if (filled)
    {
        g.setColour (colour.withAlpha (0.55f));
        g.fillPath (fill);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f), 0.0f, plot.getY(), colour.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (0.8f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

void SpectrumDisplay::drawAnalysisOverlay (juce::Graphics& g)
{
    if (! showAnalysis || ctx.analysis == nullptr || ctx.analysis->status != analysis::AnalysisResult::Status::Ok)
        return;

    const auto& r = *ctx.analysis;
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (plot.toNearestInt());

    // Long-term spectrum of the analysed audio, calibrated like the live analyser: same bin width
    // (noise level depends on it) and the same display slope.
    const auto& settings = postAnalyzer.getSettings();
    const double liveBinHz = r.sampleRate / (double) (1 << settings.fftOrder);
    const float binOffset = (float) (10.0 * std::log10 (liveBinHz / std::max (1.0e-6, r.ltasBinHz)));
    juce::Path ltas;
    bool started = false;
    for (size_t i = 0; i < r.ltasDb.size(); ++i)
    {
        if (r.ltasDb[i] <= -139.0f)
            continue;
        const double hz = r.displayFrequencies[i];
        const float level = r.ltasDb[i] + binOffset + settings.slopeDbPerOctave * (float) std::log2 (hz / 1000.0);
        const float x = xForFrequency (hz);
        const float y = yForAnalyzerDb (level);
        if (! started)
            ltas.startNewSubPath (x, y), started = true;
        else
            ltas.lineTo (x, y);
    }
    if (started)
    {
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, ltas, dashes, 2);
        g.setColour (juce::Colour (0xffc9a24d).withAlpha (0.55f));
        g.fillPath (dashed);
    }

    // Typical balance of the chosen instrument, aligned with the measurement, with its tolerance band.
    const auto settingsNow = ctx.processor.getCorrectionSettings();
    const auto cmp = correction::compareToTarget (r, settingsNow.instrument, settingsNow.strength);
    if (cmp.valid)
    {
        juce::Path line, band;
        juce::Array<juce::Point<float>> upper, lower;
        for (size_t i = 0; i < cmp.frequencies.size(); ++i)
        {
            const double hz = cmp.frequencies[i];
            // back from per-1/3-octave to the per-bin scale the overlay uses
            const float base = (float) (cmp.targetDb[i] - 10.0 * std::log10 (hz / 1000.0)) + binOffset
                             + settings.slopeDbPerOctave * (float) std::log2 (hz / 1000.0);
            const float x = xForFrequency (hz);
            if (i == 0)
                line.startNewSubPath (x, yForAnalyzerDb (base));
            else
                line.lineTo (x, yForAnalyzerDb (base));
            upper.add ({ x, yForAnalyzerDb (base + (float) cmp.toleranceDb) });
            lower.add ({ x, yForAnalyzerDb (base - (float) cmp.toleranceDb) });
        }
        for (int i = 0; i < upper.size(); ++i)
            i == 0 ? band.startNewSubPath (upper[i]) : band.lineTo (upper[i]);
        for (int i = lower.size(); --i >= 0;)
            band.lineTo (lower[i]);
        band.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.06f));
        g.fillPath (band);
        g.setColour (colours::accent.withAlpha (0.55f));
        g.strokePath (line, juce::PathStrokeType (1.4f));
        if (! cmp.frequencies.empty())
        {
            g.setFont (uiFont (10.5f));
            g.drawText ("typical " + correction::instrumentName (settingsNow.instrument).toLowerCase(),
                        juce::Rectangle<float> (xForFrequency (cmp.frequencies.front()) + 4.0f, plot.getY() + 14.0f, 200.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // Findings
    for (int i = 0; i < (int) r.findings.size(); ++i)
    {
        const auto& f = r.findings[(size_t) i];
        if (f.type == analysis::FindingType::DCOffset)
            continue;

        const bool selected = ctx.selectedFinding == i;
        const auto colour = colourForSeverity (f.severityLabel());
        const float top = plot.getY();

        if (f.isNarrow())
        {
            const float x = xForFrequency (f.frequency);
            juce::Path line, dashed;
            line.startNewSubPath (x, top + 12.0f);
            line.lineTo (x, plot.getBottom());
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (selected ? 1.6f : 1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (colour.withAlpha (selected ? 0.9f : 0.45f));
            g.fillPath (dashed);

            juce::Path diamond;
            diamond.addPolygon ({ x, top + 6.0f }, 4, selected ? 5.5f : 4.0f, juce::MathConstants<float>::pi * 0.25f);
            g.setColour (colour.withAlpha (selected ? 1.0f : 0.8f));
            g.fillPath (diamond);
        }
        else
        {
            const float x0 = xForFrequency (std::max (analysis::AnalysisSettings().displayMinHz, f.lowFrequency));
            const float x1 = xForFrequency (f.highFrequency);
            g.setColour (colour.withAlpha (selected ? 0.13f : 0.05f));
            g.fillRect (juce::Rectangle<float> (x0, top, x1 - x0, plot.getHeight()));
            g.setColour (colour.withAlpha (selected ? 0.9f : 0.6f));
            g.fillRect (juce::Rectangle<float> (x0, top, x1 - x0, 3.0f));
        }

        if (selected)
        {
            const auto text = f.title + (f.isNarrow() ? juce::String() : "  " + analysis::describeFrequency (f.frequency));
            g.setFont (uiFont (12.0f, true));
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (uiFont (12.0f, true), text) + 16.0f;
            auto box = juce::Rectangle<float> (w, 20.0f).withCentre ({ xForFrequency (f.frequency), top + 24.0f });
            box = box.withX (juce::jlimit (plot.getX() + 2.0f, plot.getRight() - w - 2.0f, box.getX()));
            g.setColour (colours::panelRaised.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (box, 4.0f, 1.0f);
            g.drawText (text, box.toNearestInt(), juce::Justification::centred, false);
        }
    }
}

void SpectrumDisplay::drawReadout (juce::Graphics& g, int band)
{
    const auto& p = cache[(size_t) band].params;
    const auto pos = nodePosition (band);

    juce::StringArray lines;
    lines.add ("Band " + juce::String (band + 1) + "  " + params::filterTypeNames()[(int) p.type]);
    juce::String detail = formatHz (p.frequency);
    if (dsp::typeUsesGain (p.type))
        detail << "   " << formatGain (p.gainDb);
    lines.add (detail);
    if (dsp::typeUsesQ (p.type, p.slopeIndex))
        lines.add ("Q " + juce::String (p.q, p.q < 1.0 ? 3 : 2)
                   + (dsp::typeUsesSlope (p.type) ? "   " + params::slopeNames()[p.slopeIndex] : juce::String()));
    else if (dsp::typeUsesSlope (p.type))
        lines.add (params::slopeNames()[p.slopeIndex]);

    const float w = 150.0f, lineH = 15.0f;
    const float h = lineH * (float) lines.size() + 8.0f;
    auto box = juce::Rectangle<float> (w, h).withPosition (pos.x + 14.0f, pos.y - h - 10.0f);
    if (box.getRight() > plot.getRight())
        box.setX (pos.x - w - 14.0f);
    if (box.getY() < plot.getY())
        box.setY (pos.y + 14.0f);

    g.setColour (colours::panelRaised.withAlpha (0.94f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (colours::forBand (band).withAlpha (0.6f));
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    g.setFont (uiFont (12.5f));
    for (int i = 0; i < lines.size(); ++i)
    {
        g.setColour (i == 0 ? colours::forBand (band) : colours::text);
        g.drawText (lines[i], box.reduced (8.0f, 4.0f).withHeight (lineH).translated (0.0f, lineH * (float) i).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int hit = hitTestNode (e.position);
    if (hit != hoveredBand)
        hoveredBand = hit;
    setMouseCursor (hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    mousePos = { -1.0f, -1.0f };
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (auto* top = getTopLevelComponent())
        top->grabKeyboardFocus();

    endWheelGesture();
    const int hit = hitTestNode (e.position);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            if (! ctx.selection.isSelected (hit))
                ctx.selection.selectOnly (hit);
            showBandMenu (hit);
        }
        return;
    }

    if (hit >= 0)
    {
        if (e.mods.isShiftDown())
        {
            if (ctx.selection.isSelected (hit))
            {
                ctx.selection.deselect (hit);
                repaint();
                return;
            }
            ctx.selection.addToSelection (hit);
        }
        else if (! ctx.selection.isSelected (hit))
        {
            ctx.selection.selectOnly (hit);
        }
        else
        {
            // Make the clicked node the primary band while keeping the group.
            ctx.selection.deselect (hit);
            ctx.selection.addToSelection (hit);
        }

        dragMode = DragMode::Nodes;
        dragOrigin = e.position;
        dragStarts.clear();

        ctx.processor.history.beginEdit();
        for (int b : ctx.selection)
        {
            const auto& p = cache[(size_t) b].params;
            dragStarts.push_back ({ b, std::log (p.frequency), (float) p.gainDb });
            ctx.param (b, params::BandField::Frequency)->beginChangeGesture();
            ctx.param (b, params::BandField::Gain)->beginChangeGesture();
        }
    }
    else
    {
        dragMode = DragMode::Lasso;
        dragOrigin = e.position;
        lasso = {};
        lassoBaseSelection.clear();
        if (e.mods.isShiftDown())
            lassoBaseSelection = ctx.selection.getItemArray();
        else
            ctx.selection.deselectAll();
    }

    repaint();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;

    if (dragMode == DragMode::Nodes)
    {
        const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
        const auto delta = (e.position - dragOrigin) * fine;
        const double logRange = std::log (kMaxHz / kMinHz);
        const double dLogF = (double) (delta.x / plot.getWidth()) * logRange;
        const double dGain = -(double) (delta.y / (plot.getHeight() * 0.5f)) * rangeDb;

        for (const auto& s : dragStarts)
        {
            auto* fp = ctx.param (s.band, params::BandField::Frequency);
            auto* gp = ctx.param (s.band, params::BandField::Gain);
            const double f = juce::jlimit (dsp::kMinFrequency, dsp::kMaxFrequency, std::exp (s.logFrequency + dLogF));
            fp->setValueNotifyingHost (fp->convertTo0to1 ((float) f));

            if (dsp::typeUsesGain (cache[(size_t) s.band].params.type))
            {
                const double gdb = juce::jlimit (-dsp::kMaxGainDb, dsp::kMaxGainDb, (double) s.gain + dGain);
                gp->setValueNotifyingHost (gp->convertTo0to1 ((float) gdb));
            }
        }
        refreshCurves (false);
    }
    else if (dragMode == DragMode::Lasso)
    {
        lasso = juce::Rectangle<float> (dragOrigin, e.position);
        juce::Array<int> selected (lassoBaseSelection);
        for (int b = 0; b < params::kNumBands; ++b)
            if (cache[(size_t) b].params.used && lasso.contains (nodePosition (b)))
                selected.addIfNotAlreadyThere (b);

        ctx.selection.deselectAll();
        for (int b : selected)
            ctx.selection.addToSelection (b);
        repaint();
    }
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::Nodes)
    {
        for (const auto& s : dragStarts)
        {
            ctx.param (s.band, params::BandField::Frequency)->endChangeGesture();
            ctx.param (s.band, params::BandField::Gain)->endChangeGesture();
        }
        dragStarts.clear();
    }

    dragMode = DragMode::None;
    lasso = {};
    repaint();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! plot.contains (e.position) || e.mods.isPopupMenu())
        return;

    const int hit = hitTestNode (e.position);
    if (hit >= 0)
    {
        ctx.processor.history.beginEdit();
        state::EditHistory::ScopedSuspend suspend (ctx.processor.history);
        ctx.setValue (hit, params::BandField::Bypass, ctx.value (hit, params::BandField::Bypass) >= 0.5f ? 0.0f : 1.0f);
        return;
    }

    const double f = frequencyForX (e.position.x);
    const double gdb = juce::jlimit (-dsp::kMaxGainDb, dsp::kMaxGainDb, gainForY (e.position.y));
    const int b = ctx.createBand (dsp::FilterType::Bell, (float) f, (float) std::round (gdb * 10.0) / 10.0f);
    if (b >= 0)
    {
        ctx.selection.selectOnly (b);
        refreshCurves (false);
        hoveredBand = b;
    }
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = hitTestNode (e.position);
    if (band < 0)
        band = ctx.primaryBand();
    if (band < 0 || ! cache[(size_t) band].params.used)
        return;

    const auto& p = cache[(size_t) band].params;
    if (! dsp::typeUsesQ (p.type, p.slopeIndex))
        return;

    if (wheelBand != band)
    {
        endWheelGesture();
        ctx.processor.history.beginEdit();
        wheelBand = band;
        ctx.param (band, params::BandField::Q)->beginChangeGesture();
    }
    lastWheelTime = juce::Time::getMillisecondCounterHiRes();

    const float delta = (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    const double factor = std::pow (2.0, (double) delta * (e.mods.isShiftDown() ? 0.25 : 1.0));
    const double q = juce::jlimit (dsp::kMinQ, dsp::kMaxQ, p.q * factor);

    auto* qp = ctx.param (band, params::BandField::Q);
    qp->setValueNotifyingHost (qp->convertTo0to1 ((float) q));
    refreshCurves (false);
}

void SpectrumDisplay::endWheelGesture()
{
    if (wheelBand >= 0)
    {
        if (auto* qp = ctx.param (wheelBand, params::BandField::Q))
            qp->endChangeGesture();
        wheelBand = -1;
    }
}

void SpectrumDisplay::showBandMenu (int band)
{
    using params::BandField;
    const auto p = ctx.readBand (band);

    juce::PopupMenu typeMenu, slopeMenu, placeMenu;
    const auto typeNames = params::filterTypeNames();
    for (int i = 0; i < typeNames.size(); ++i)
        typeMenu.addItem (100 + i, typeNames[i], true, (int) p.type == i);

    const auto slopes = params::slopeNames();
    for (int i = 0; i < slopes.size(); ++i)
        slopeMenu.addItem (200 + i, slopes[i], true, p.slopeIndex == i);

    const auto places = params::placementNames();
    for (int i = 0; i < places.size(); ++i)
        placeMenu.addItem (300 + i, places[i], true, (int) p.placement == i);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (band + 1));
    menu.addSubMenu ("Filter type", typeMenu);
    menu.addSubMenu ("Slope", slopeMenu, dsp::typeUsesSlope (p.type));
    menu.addSubMenu ("Stereo placement", placeMenu);
    menu.addSeparator();
    menu.addItem (1, "Bypass", true, p.bypassed);
    menu.addItem (2, "Solo (audition)", true, ctx.processor.soloBand.load() == band);
    menu.addItem (3, "Reset gain to 0 dB", dsp::typeUsesGain (p.type));
    menu.addSeparator();
    menu.addItem (4, ctx.selection.getNumSelected() > 1 ? "Delete selected bands" : "Delete band");

    juce::Component::SafePointer<SpectrumDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe, band] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& c = safe->ctx;

                            if (result == 2)
                            {
                                c.processor.soloBand = (c.processor.soloBand.load() == band) ? -1 : band;
                                safe->repaint();
                                return;
                            }

                            if (result == 4)
                            {
                                c.deleteBands (c.selection.getItemArray());
                                return;
                            }

                            c.processor.history.beginEdit();
                            state::EditHistory::ScopedSuspend suspend (c.processor.history);

                            if (result == 1)
                                c.setValue (band, BandField::Bypass, c.value (band, BandField::Bypass) >= 0.5f ? 0.0f : 1.0f);
                            else if (result == 3)
                                c.setValue (band, BandField::Gain, 0.0f);
                            else if (result >= 100 && result < 200)
                                c.setValue (band, BandField::Type, (float) (result - 100));
                            else if (result >= 200 && result < 300)
                                c.setValue (band, BandField::Slope, (float) (result - 200));
                            else if (result >= 300 && result < 400)
                                c.setValue (band, BandField::Placement, (float) (result - 300));
                        });
}

} // namespace eyra::ui
