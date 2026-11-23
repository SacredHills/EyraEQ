#include "EqBandPanel.h"

#include "EyraLookAndFeel.h"

namespace eyra::ui
{

EqBandPanel::EqBandPanel (EditorContext& context) : ctx (context)
{
    bandLabel.setFont (uiFont (15.0f, true));
    addAndMakeVisible (bandLabel);
    for (auto* b : { &typeBox, &slopeBox, &dynBox })
        addAndMakeVisible (*b);
    typeBox.addItemList (dsp::eqTypeNames, 1);
    slopeBox.addItemList (dsp::eqSlopeNames, 1);
    dynBox.addItemList (dsp::eqDynNames, 1);
    typeBox.setTooltip ("Filter type");
    slopeBox.setTooltip ("How steeply the cut falls away (dB per octave)");
    dynBox.setTooltip ("Off: fixed EQ.  Dynamic: the band's gain moves with the level in the band (above the threshold it "
                       "moves toward Gain + Range).  Spectral: the same, but frequency by frequency inside the band, so only "
                       "the frequencies that poke out are moved (adds latency).");

    onButton.setClickingTogglesState (true);
    onButton.setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.3f));
    onButton.setTooltip ("Band on / bypassed");
    addAndMakeVisible (onButton);

    removeButton.onClick = [this]
    {
        if (bound < 0)
            return;
        ctx.processor.history.beginEdit();
        const state::EditHistory::ScopedSuspend suspend (ctx.processor.history);
        params::clearBand (ctx.state, bound);
        ctx.selectedBand = -1;
        showBand (-1);
    };
    addAndMakeVisible (removeButton);

    listenButton.setColour (juce::TextButton::buttonOnColourId, colours::warning.withAlpha (0.25f));
    listenButton.setTooltip ("Listen: hear only the part of the sound this band works on (what it boosts, cuts or removes). "
                             "Tip: hold Alt while dragging a band to listen while you move it.");
    listenButton.onClick = [this]
    {
        auto& eq = ctx.processor.getEq();
        eq.listenBand = eq.listenBand.load() == bound ? -1 : bound;
        syncListen();
    };
    addAndMakeVisible (listenButton);

    const char* names[] { "Frequency", "Gain", "Q", "Threshold", "Range", "Attack", "Release", "Output" };
    for (int k = 0; k < 8; ++k)
    {
        auto& kn = knobs[(size_t) k];
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
        kn.slider.setColour (juce::Slider::rotarySliderFillColourId, k >= 3 && k < 7 ? colours::warning : colours::accent);
        kn.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        kn.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        kn.label.setText (names[k], juce::dontSendNotification);
        kn.label.setJustificationType (juce::Justification::centred);
        kn.label.setFont (uiFont (10.5f, true));
        kn.label.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (kn.slider);
        addAndMakeVisible (kn.label);
    }
    knobs[3].slider.setTooltip ("Level in the band where the dynamics start to act");
    knobs[4].slider.setTooltip ("How far the dynamics can move the gain: negative turns loud parts down, positive turns them up");
    knobs[5].slider.setTooltip ("How fast the dynamics react when the level rises (this band)");
    knobs[6].slider.setTooltip ("How fast the dynamics let go when the level falls (this band)");
    autoRelButton.setTooltip ("The release follows the playing: slow within a sustained note (no pumping), fast in the gaps between chugs. "
                              "The Release knob is not used.");
    addChildComponent (autoRelButton);

    if (auto* out = ctx.state.getParameter (params::id::outputGain))
    {
        knobs[7].attach = std::make_unique<juce::SliderParameterAttachment> (*out, knobs[7].slider);
        knobs[7].slider.setDoubleClickReturnValue (true, out->convertFrom0to1 (out->getDefaultValue()));
    }
    showBand (-1);
}

EqBandPanel::~EqBandPanel()
{
    ctx.processor.getEq().listenBand = -1;
}

void EqBandPanel::showBand (int b)
{
    auto& eq = ctx.processor.getEq();
    if (eq.listenBand.load() >= 0)
        eq.listenBand = b; // listening follows the selected band
    bound = b;

    typeAttach.reset();
    slopeAttach.reset();
    dynAttach.reset();
    autoRelAttach.reset();
    onAttach.reset();
    for (int k = 0; k < 7; ++k)
        knobs[(size_t) k].attach.reset();

    const bool has = b >= 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &bandLabel, &typeBox, &slopeBox, &dynBox, &onButton,
                                                                         &removeButton, &listenButton, &autoRelButton })
        c->setVisible (has);
    for (int k = 0; k < 7; ++k)
    {
        knobs[(size_t) k].slider.setVisible (has);
        knobs[(size_t) k].label.setVisible (has);
    }

    if (has)
    {
        using F = params::BandField;
        bandLabel.setText ("Band " + juce::String (b + 1), juce::dontSendNotification);
        bandLabel.setColour (juce::Label::textColourId, dsp::eqBandColours[b]);
        onAttach = std::make_unique<juce::ButtonParameterAttachment> (*ctx.param (b, F::On), onButton);
        typeAttach = std::make_unique<juce::ComboBoxParameterAttachment> (*ctx.param (b, F::Type), typeBox);
        slopeAttach = std::make_unique<juce::ComboBoxParameterAttachment> (*ctx.param (b, F::Slope), slopeBox);
        dynAttach = std::make_unique<juce::ComboBoxParameterAttachment> (*ctx.param (b, F::Dynamics), dynBox);
        autoRelAttach = std::make_unique<juce::ButtonParameterAttachment> (*ctx.param (b, F::AutoRelease), autoRelButton);
        const F map[7] { F::Frequency, F::Gain, F::Q, F::Threshold, F::Range, F::Attack, F::Release };
        for (int k = 0; k < 7; ++k)
        {
            auto* p = ctx.param (b, map[k]);
            knobs[(size_t) k].attach = std::make_unique<juce::SliderParameterAttachment> (*p, knobs[(size_t) k].slider);
            knobs[(size_t) k].slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }
    }
    refresh();
    repaint();
}

/** Controls that do nothing for the band's type / dynamics mode are dimmed. */
void EqBandPanel::refresh()
{
    syncListen();
    if (bound < 0)
        return;
    const auto band = ctx.processor.getEq().settings().b[(size_t) bound];
    const int t = band.typeI();
    slopeBox.setEnabled (dsp::eqIsCut (t));
    knobs[1].slider.setEnabled (dsp::eqHasGain (t));
    knobs[1].label.setEnabled (dsp::eqHasGain (t));
    const bool dyn = band.dynamic() || band.spectral();
    for (int k = 3; k < 7; ++k)
    {
        knobs[(size_t) k].slider.setEnabled (dyn);
        knobs[(size_t) k].label.setEnabled (dyn);
    }
    const bool manualRel = dyn && band.arel < 0.5f;
    knobs[6].slider.setEnabled (manualRel);
    knobs[6].label.setEnabled (manualRel);
    autoRelButton.setEnabled (dyn);
    dynBox.setEnabled (! dsp::eqIsCut (t));
}

void EqBandPanel::syncListen()
{
    const int l = ctx.processor.getEq().listenBand.load();
    listenButton.setToggleState (l >= 0 && l == bound, juce::dontSendNotification);
}

void EqBandPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bandPanel.toFloat(), 6.0f);
    g.fillRoundedRectangle (globalPanel.toFloat(), 6.0f);
    g.setColour (colours::textDim);
    if (bound >= 0)
    {
        g.setFont (uiFont (10.5f, true));
        g.drawText ("Dynamics", dynLabelArea, juce::Justification::centredLeft);
    }
    else
    {
        g.setFont (uiFont (13.0f));
        g.drawText ("Select a band (click its point) to edit it, or double-click the graph to add one.", bandPanel,
                    juce::Justification::centred);
    }
}

void EqBandPanel::resized()
{
    auto r = getLocalBounds();
    globalPanel = r.removeFromRight (96);
    r.removeFromRight (8);
    bandPanel = r;

    auto bp = bandPanel.reduced (10, 8);
    auto left = bp.removeFromLeft (150);
    {
        auto lr = left.removeFromTop (24);
        listenButton.setBounds (lr.removeFromRight (64));
        bandLabel.setBounds (lr);
    }
    left.removeFromTop (4);
    auto row = left.removeFromTop (26);
    onButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (6);
    removeButton.setBounds (row);
    left.removeFromTop (6);
    typeBox.setBounds (left.removeFromTop (26));
    left.removeFromTop (6);
    slopeBox.setBounds (left.removeFromTop (26));
    bp.removeFromLeft (10);
    const int w = 74;
    for (int k = 0; k < 3; ++k)
        placeKnob (k, bp.removeFromLeft (w));
    bp.removeFromLeft (12);
    auto dynCol = bp.removeFromLeft (110);
    dynCol.removeFromTop (18);
    dynLabelArea = dynCol.removeFromTop (16);
    dynBox.setBounds (dynCol.removeFromTop (26));
    dynCol.removeFromTop (4);
    autoRelButton.setBounds (dynCol.removeFromTop (22));
    for (int k = 3; k < 7; ++k)
        placeKnob (k, bp.removeFromLeft (w));
    placeKnob (7, globalPanel.reduced (8, 8));
}

void EqBandPanel::placeKnob (int k, juce::Rectangle<int> cell)
{
    auto& kn = knobs[(size_t) k];
    kn.label.setBounds (cell.removeFromTop (16));
    kn.slider.setBounds (cell.reduced (2, 0));
}

} // namespace eyra::ui
