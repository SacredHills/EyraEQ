#pragma once

#include "Analysis/SpectrumAnalyzer.h"
#include "EditorContext.h"

namespace eyra::ui
{

/**
    The main view: live input / output spectrum, the EQ response, and the editable band nodes.

    Mouse:
      double-click empty space   create a bell band there
      drag a node                frequency / gain (Shift = fine). All selected nodes move together.
      Shift-click                add / remove a node from the selection
      drag empty space           lasso selection
      mouse wheel over a node    Q (or the selected band when not over a node)
      double-click a node        toggle bypass
      right-click a node         type, slope, stereo placement, bypass, solo, delete
*/
class SpectrumDisplay final : public juce::Component
{
public:
    explicit SpectrumDisplay (EditorContext& context);

    /** Called by the editor timer (~60 Hz). */
    void tick (double elapsedSeconds);

    void applyUIState (const EyraUIState& state);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Mapping helpers (public for tests). */
    float xForFrequency (double hz) const noexcept;
    double frequencyForX (float x) const noexcept;
    float yForGain (double db) const noexcept;
    double gainForY (float y) const noexcept;

private:
    struct CachedBand
    {
        dsp::BandParams params;
        bool valid = false;
        std::vector<float> curveDb;
    };

    void refreshCurves (bool force);
    juce::Point<float> nodePosition (int band) const;
    int hitTestNode (juce::Point<float> p) const;
    void showBandMenu (int band);
    void endWheelGesture();
    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&, const std::vector<float>& levels, juce::Colour colour, bool filled);
    void drawReadout (juce::Graphics&, int band);
    float yForAnalyzerDb (float db) const noexcept;

    EditorContext& ctx;
    analysis::SpectrumAnalyzer preAnalyzer, postAnalyzer;
    bool showPre = true, showPost = true;
    float rangeDb = 18.0f;

    juce::Rectangle<float> plot;
    std::vector<double> curveFrequencies;
    std::array<CachedBand, params::kNumBands> cache;
    std::vector<float> compositeDb;
    double cachedSampleRate = 0.0;

    int hoveredBand = -1;
    juce::Point<float> mousePos { -1.0f, -1.0f };

    enum class DragMode { None, Nodes, Lasso };
    DragMode dragMode = DragMode::None;
    struct DragStart
    {
        int band;
        double logFrequency;
        float gain;
    };
    std::vector<DragStart> dragStarts;
    juce::Point<float> dragOrigin;
    juce::Rectangle<float> lasso;
    juce::Array<int> lassoBaseSelection;

    int wheelBand = -1;
    double lastWheelTime = 0.0;
};

} // namespace eyra::ui
