#include "EqGraph.h"

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


float EqGraph::xForFrequency (double hz) const noexcept
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + (float) t * plot.getWidth();
}

double EqGraph::frequencyForX (float x) const noexcept
{
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / plot.getWidth()));
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float EqGraph::yForGain (double db) const noexcept
{
    return plot.getCentreY() - (float) (db / rangeDb) * plot.getHeight() * 0.5f;
}

double EqGraph::gainForY (float y) const noexcept
{
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * rangeDb;
}

float EqGraph::yForAnalyzerDb (float db) const noexcept
{
    const float t = (kAnalyzerTopDb - db) / (kAnalyzerTopDb - kAnalyzerBottomDb);
    return plot.getY() + juce::jlimit (0.0f, 1.0f, t) * plot.getHeight();
}


void EqGraph::drawGrid (juce::Graphics& g)
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

void EqGraph::drawSpectrum (juce::Graphics& g, const std::vector<float>& levels, juce::Colour colour, bool filled)
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

void EqGraph::drawAnalysisOverlay (juce::Graphics& g)
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

EqGraph::EqGraph (EditorContext& context)
    : ctx (context), preAnalyzer (context.processor.preFifo), postAnalyzer (context.processor.postFifo)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    applyUIState (ctx.processor.getUIState());
}

void EqGraph::applyUIState (const EyraUIState& s)
{
    preAnalyzer.setSettings (s.analyzer);
    postAnalyzer.setSettings (s.analyzer);
    showPre = s.showPreSpectrum;
    showPost = s.showPostSpectrum;
    showAnalysis = s.showAnalysisOverlay;
    rangeDb = s.displayRangeDb;
    repaint();
}

void EqGraph::select (int band)
{
    ctx.selectedBand = band;
    if (onSelect)
        onSelect (band);
    repaint();
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().withTrimmedRight (kRightMargin).withTrimmedBottom (kBottomMargin).reduced (1.0f, 8.0f);
}

void EqGraph::tick (double elapsedSeconds)
{
    const bool a = preAnalyzer.update (elapsedSeconds) && showPre;
    const bool b = postAnalyzer.update (elapsedSeconds) && showPost;

    // Drop the selection if its band was removed elsewhere (undo, presets, automation).
    if (ctx.selectedBand >= 0 && ctx.value (ctx.selectedBand, params::BandField::Used) < 0.5f)
        select (-1);

    // Dynamic bands move on their own, so keep drawing while any is active.
    const auto s = ctx.processor.getEq().settings();
    bool live = false;
    for (const auto& band : s.b)
        live |= band.dynamic() || band.spectral();

    if (a || b || live)
        repaint();
}

juce::Point<float> EqGraph::nodePosition (int band, const dsp::EqAll& s) const
{
    const auto& b = s.b[(size_t) band];
    const float x = xForFrequency (juce::jlimit (kMinHz, kMaxHz, (double) b.freq));
    const float y = yForGain (dsp::eqHasGain (b.typeI()) ? b.gain : 0.0f);
    return { x, juce::jlimit (plot.getY() + 8.0f, plot.getBottom() - 8.0f, y) };
}

int EqGraph::hitTestNode (juce::Point<float> pos) const
{
    const auto s = ctx.processor.getEq().settings();
    int best = -1;
    float bestD = 14.0f;
    for (int b = 0; b < params::kNumBands; ++b)
        if (s.b[(size_t) b].isUsed())
            if (const float d = nodePosition (b, s).getDistanceFrom (pos); d < bestD)
                bestD = d, best = b;
    return best;
}

void EqGraph::paint (juce::Graphics& g)
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

    auto& eq = ctx.processor.getEq();
    const auto s = eq.settings();
    const double sr = juce::jmax (44100.0, ctx.processor.getEngineSampleRate());
    const bool bypassed = ctx.processor.apvts.getRawParameterValue (params::id::masterBypass)->load() >= 0.5f;

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    std::array<dsp::EqBandFilter, dsp::eqBandCount> stat, live;
    for (int b = 0; b < dsp::eqBandCount; ++b)
    {
        const auto& band = s.b[(size_t) b];
        stat[(size_t) b].design (band, sr, band.gain);
        live[(size_t) b].design (band, sr, band.gain + (band.dynamic() ? eq.dynNow[(size_t) b].load() : 0.0f));
    }

    auto dbOf = [&] (const dsp::EqBandFilter& f, float x)
    {
        return (float) juce::Decibels::gainToDecibels (f.magnitude (frequencyForX (x), sr), -60.0);
    };
    auto curveOf = [&] (auto&& dbAt)
    {
        juce::Path p;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float y = juce::jlimit (plot.getY() - 40.0f, plot.getBottom() + 40.0f, yForGain (dbAt (x)));
            if (x == plot.getX())
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        return p;
    };

    // The selected band: its own curve, and for dynamics the range it can move through.
    const int sel = ctx.selectedBand;
    if (sel >= 0 && s.b[(size_t) sel].isOn())
    {
        const auto& band = s.b[(size_t) sel];
        const auto c = dsp::eqBandColours[sel];
        if (band.dynamic() || band.spectral())
        {
            dsp::EqBandFilter reach;
            reach.design (band, sr, band.gain + band.range);
            juce::Path area = curveOf ([&] (float x) { return dbOf (stat[(size_t) sel], x); });
            for (float x = plot.getRight(); x >= plot.getX(); x -= 2.0f)
                area.lineTo (x, yForGain (dbOf (reach, x)));
            area.closeSubPath();
            g.setColour (c.withAlpha (0.16f));
            g.fillPath (area);
        }
        g.setColour (c.withAlpha (0.8f));
        g.strokePath (curveOf ([&] (float x) { return dbOf (live[(size_t) sel], x); }), juce::PathStrokeType (1.2f));
    }

    // Spectral dynamics at work: how much each frequency is being moved right now.
    if (eq.spectralOn.load())
    {
        juce::Path sp;
        const int pts = dsp::EqSpectral::displayPoints;
        for (int d = 0; d < pts; ++d)
        {
            const float x = xForFrequency (20.0 * std::pow (1000.0, (double) d / (pts - 1)));
            const float y = yForGain (juce::jlimit (-rangeDb, rangeDb, eq.spectral.display[(size_t) d].load()));
            d == 0 ? sp.startNewSubPath (x, y) : sp.lineTo (x, y);
        }
        juce::Path fill (sp);
        fill.lineTo (xForFrequency (20000.0), yForGain (0.0));
        fill.lineTo (xForFrequency (20.0), yForGain (0.0));
        fill.closeSubPath();
        g.setColour (colours::danger.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (colours::danger.withAlpha (0.7f));
        g.strokePath (sp, juce::PathStrokeType (1.0f));
    }

    // Total response
    {
        const float outDb = s.out;
        auto total = curveOf ([&] (float x)
        {
            double db = outDb;
            for (int b = 0; b < dsp::eqBandCount; ++b)
                if (s.b[(size_t) b].isOn())
                    db += dbOf (live[(size_t) b], x);
            return (float) db;
        });
        g.setColour (colours::curve.withAlpha (bypassed ? 0.25f : 0.1f));
        g.strokePath (total, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::curve.withAlpha (bypassed ? 0.35f : 0.95f));
        g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Nodes
    for (int b = 0; b < dsp::eqBandCount; ++b)
    {
        const auto& band = s.b[(size_t) b];
        if (! band.isUsed())
            continue;
        const auto p = nodePosition (b, s);
        const auto c = dsp::eqBandColours[b];
        const bool hot = b == sel || b == hovered || b == dragging;
        g.setFont (uiFont (10.5f, true));
        if (! band.isOn()) // bypassed: a hollow point, still there to grab and switch back on
        {
            g.setColour (colours::background);
            g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
            g.setColour (c.withAlpha (hot ? 0.9f : 0.5f));
            g.drawEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f, 1.5f);
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f), juce::Justification::centred);
            if (b == sel)
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawEllipse (p.x - 10.0f, p.y - 10.0f, 20.0f, 20.0f, 1.0f);
            }
            continue;
        }
        if (band.dynamic() || band.spectral()) // a ring marks a dynamic band
        {
            g.setColour (c.withAlpha (0.9f));
            g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 1.5f);
            if (band.dynamic())
            {
                g.setColour (c.withAlpha (0.6f));
                g.drawLine (p.x, p.y, p.x, yForGain (band.gain + eq.dynNow[(size_t) b].load()), 2.0f);
            }
        }
        g.setColour (c.withAlpha (hot ? 1.0f : 0.85f));
        g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
        if (b == sel)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (p.x - 9.5f, p.y - 9.5f, 19.0f, 19.0f, 1.5f);
        }
        g.setColour (colours::background);
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f), juce::Justification::centred);
    }

    const int info = dragging >= 0 ? dragging : hovered;
    if (info >= 0 && s.b[(size_t) info].isUsed())
        drawReadout (g, info, s);
    else if (! std::any_of (s.b.begin(), s.b.end(), [] (const auto& b) { return b.isUsed(); }))
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText ("Double-click to add a band", plot.withTrimmedBottom (plot.getHeight() * 0.3f), juce::Justification::centred);
    }
}

void EqGraph::drawReadout (juce::Graphics& g, int band, const dsp::EqAll& s)
{
    const auto& b = s.b[(size_t) band];
    juce::String t = dsp::eqTypeNames[b.typeI()] + "  " + formatHz (b.freq);
    if (dsp::eqHasGain (b.typeI()))
        t << "  " << formatGain (b.gain);
    if (dsp::eqIsCut (b.typeI()))
        t << "  " << dsp::eqSlopeNames[juce::jlimit (0, 7, juce::roundToInt (b.slope))];
    else
        t << "  Q " << juce::String (b.q, 2);
    if (b.dynamic() || b.spectral())
        t << "  (" << dsp::eqDynNames[b.dynI()] << ")";
    if (! b.isOn())
        t << "  (bypassed)";

    const auto p = nodePosition (band, s);
    const auto font = uiFont (12.0f, true);
    g.setFont (font);
    auto box = juce::Rectangle<float> (p.x + 12.0f, p.y - 30.0f, (float) juce::GlyphArrangement::getStringWidthInt (font, t) + 14.0f, 20.0f)
                   .constrainedWithin (plot.reduced (4.0f));
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (dsp::eqBandColours[band]);
    g.drawText (t, box, juce::Justification::centred);
}

//==============================================================================
void EqGraph::setBand (int band, params::BandField field, float value)
{
    params::setParameterValue (ctx.state, params::bandParamID (band, field), value);
}

void EqGraph::removeBand (int band)
{
    ctx.processor.history.beginEdit();
    const state::EditHistory::ScopedSuspend suspend (ctx.processor.history);
    params::clearBand (ctx.state, band);
    if (ctx.selectedBand == band)
        select (-1);
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int b = hitTestNode (e.position);
    if (e.mods.isPopupMenu())
    {
        if (b >= 0)
        {
            select (b);
            showBandMenu (b);
        }
        return;
    }
    select (b);
    dragging = b;
    if (b < 0)
        return;
    if (e.mods.isAltDown()) // Alt-drag: hear the band while moving it
    {
        ctx.processor.getEq().listenBand = b;
        altListening = true;
    }
    for (auto f : { params::BandField::Frequency, params::BandField::Gain })
        ctx.param (b, f)->beginChangeGesture();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    using F = params::BandField;
    const auto s = ctx.processor.getEq().settings();
    const auto& band = s.b[(size_t) dragging];
    auto put = [&] (F f, float v)
    {
        auto* p = ctx.param (dragging, f);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    float f = (float) frequencyForX (e.position.x);
    if (e.mods.isShiftDown())
        f = band.freq; // Shift: gain only
    put (F::Frequency, juce::jlimit (10.0f, 30000.0f, f));
    if (dsp::eqHasGain (band.typeI()) && ! e.mods.isCommandDown()) // Ctrl: frequency only
        put (F::Gain, (float) juce::jlimit (-(double) dsp::eqGainRange, (double) dsp::eqGainRange, gainForY (e.position.y)));
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto f : { params::BandField::Frequency, params::BandField::Gain })
            ctx.param (dragging, f)->endChangeGesture();
    dragging = -1;
    if (altListening)
    {
        altListening = false;
        ctx.processor.getEq().listenBand = -1;
        if (onListenChanged)
            onListenChanged();
    }
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    using F = params::BandField;
    const int b = hitTestNode (e.position);
    if (b >= 0)
    {
        removeBand (b);
        return;
    }
    if (! plot.contains (e.position))
        return;

    int free = -1;
    for (int k = 0; k < params::kNumBands && free < 0; ++k)
        if (ctx.value (k, F::Used) < 0.5f)
            free = k;
    if (free < 0)
        return;

    const float f = (float) frequencyForX (e.position.x);
    const int type = f < 35.0f ? dsp::eqLowCut : f > 15000.0f ? dsp::eqHighCut : dsp::eqBell;

    ctx.processor.history.beginEdit();
    {
        const state::EditHistory::ScopedSuspend suspend (ctx.processor.history);
        ctx.setValue (free, F::Type, (float) type);
        ctx.setValue (free, F::Frequency, f);
        ctx.setValue (free, F::Gain, type == dsp::eqBell ? (float) juce::jlimit (-(double) dsp::eqGainRange, (double) dsp::eqGainRange, gainForY (e.position.y)) : 0.0f);
        ctx.setValue (free, F::Q, type == dsp::eqBell ? 1.0f : 0.7071f);
        ctx.setValue (free, F::Dynamics, 0.0f);
        ctx.setValue (free, F::On, 1.0f);
        ctx.setValue (free, F::Used, 1.0f);
    }
    select (free);
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    using F = params::BandField;
    int b = hitTestNode (e.position);
    if (b < 0)
        b = ctx.selectedBand;
    if (b < 0)
        return;
    const auto s = ctx.processor.getEq().settings();
    const auto& band = s.b[(size_t) b];
    const float step = w.deltaY > 0 ? 1.0f : -1.0f;
    if (e.mods.isShiftDown())
        setBand (b, F::Range, juce::jlimit (-dsp::eqGainRange, dsp::eqGainRange, band.range + step));
    else if (dsp::eqIsCut (band.typeI()))
        setBand (b, F::Slope, (float) juce::jlimit (0, 7, juce::roundToInt (band.slope) + (int) step));
    else
        setBand (b, F::Q, juce::jlimit (0.1f, 30.0f, band.q * (step > 0 ? 1.12f : 1.0f / 1.12f)));
}

bool EqGraph::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ctx.selectedBand >= 0)
    {
        removeBand (ctx.selectedBand);
        return true;
    }
    return false;
}

void EqGraph::showBandMenu (int b)
{
    using F = params::BandField;
    const auto s = ctx.processor.getEq().settings();
    const auto& band = s.b[(size_t) b];
    juce::PopupMenu m, types, slopes, dyn;
    for (int t = 0; t < dsp::eqTypeNames.size(); ++t)
        types.addItem (100 + t, dsp::eqTypeNames[t], true, band.typeI() == t);
    for (int t = 0; t < dsp::eqSlopeNames.size(); ++t)
        slopes.addItem (200 + t, dsp::eqSlopeNames[t], true, juce::roundToInt (band.slope) == t);
    for (int t = 0; t < dsp::eqDynNames.size(); ++t)
        dyn.addItem (300 + t, dsp::eqDynNames[t],
                     t == dsp::eqDynOff || (t == dsp::eqDynDynamic ? dsp::eqHasGain (band.typeI()) : ! dsp::eqIsCut (band.typeI())),
                     band.dynI() == t);
    m.addSubMenu ("Type", types);
    m.addSubMenu ("Slope", slopes, dsp::eqIsCut (band.typeI()));
    m.addSubMenu ("Dynamics", dyn);
    m.addSeparator();
    m.addItem (2, band.isOn() ? "Bypass band" : "Switch band on", true, ! band.isOn());
    m.addItem (1, "Delete band  (double-click)");

    juce::Component::SafePointer<EqGraph> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        if (r == 1)
            safe->removeBand (b);
        else if (r == 2)
            safe->setBand (b, F::On, safe->ctx.value (b, F::On) >= 0.5f ? 0.0f : 1.0f);
        else if (r >= 300)
            safe->setBand (b, F::Dynamics, (float) (r - 300));
        else if (r >= 200)
            safe->setBand (b, F::Slope, (float) (r - 200));
        else if (r >= 100)
            safe->setBand (b, F::Type, (float) (r - 100));
        if (r != 1)
            safe->select (b);
    });
}

} // namespace eyra::ui
