#pragma once

#include "Analysis/SpectrumAnalyzer.h"
#include "EditorContext.h"

namespace eyra::ui
{

/**
    The EQ graph: live spectrum, each band's curve and the total, dynamic / spectral activity, a node per band, and
    the analysis overlay (typical balance of the chosen instrument, findings).

    Double-click empty space: new band. Drag a node: frequency / gain (Shift: gain only, Ctrl: frequency only,
    Alt: listen to the band while moving it). Mouse wheel: Q or slope (Shift + wheel: dynamic range).
    Double-click a node or Delete: remove it. Right-click a node: type, slope, dynamics.
*/
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph (EditorContext& context);

    /** Called by the editor timer (~60 Hz). */
    void tick (double elapsedSeconds);

    void applyUIState (const EyraUIState& state);

    void select (int band);

    std::function<void (int)> onSelect;
    std::function<void()> onListenChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Mapping helpers (public for tests). */
    float xForFrequency (double hz) const noexcept;
    double frequencyForX (float x) const noexcept;
    float yForGain (double db) const noexcept;
    double gainForY (float y) const noexcept;

private:
    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&, const std::vector<float>& levels, juce::Colour colour, bool filled);
    void drawAnalysisOverlay (juce::Graphics&);
    void drawReadout (juce::Graphics&, int band, const dsp::EqAll&);
    float yForAnalyzerDb (float db) const noexcept;
    juce::Point<float> nodePosition (int band, const dsp::EqAll&) const;
    int hitTestNode (juce::Point<float>) const;
    void showBandMenu (int band);
    void setBand (int band, params::BandField field, float value);
    void removeBand (int band);

    EditorContext& ctx;
    analysis::SpectrumAnalyzer preAnalyzer, postAnalyzer;
    bool showPre = true, showPost = true, showAnalysis = true;
    float rangeDb = 18.0f;

    juce::Rectangle<float> plot;
    int hovered = -1, dragging = -1;
    bool altListening = false;
};

} // namespace eyra::ui
