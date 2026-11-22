#include "BandControlPanel.h"

#include "EyraLookAndFeel.h"

namespace eyra::ui
{

LabelledKnob::LabelledKnob (const juce::String& c) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void LabelledKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (16));
}

void LabelledKnob::paint (juce::Graphics& g)
{
    g.setFont (uiFont (10.5f, true));
    g.setColour (isEnabled() ? colours::textDim : colours::textFaint);
    g.drawText (caption, getLocalBounds().removeFromTop (14), juce::Justification::centred, false);
}

//==============================================================================
BandControlPanel::BandControlPanel (EditorContext& context)
    : ctx (context),
      inputAttachment (context.state, params::id::inputGain, inputKnob.slider),
      outputAttachment (context.state, params::id::outputGain, outputKnob.slider)
{
    typeBox.addItemList (params::filterTypeNames(), 1);
    slopeBox.addItemList (params::slopeNames(), 1);
    placementBox.addItemList (params::placementNames(), 1);

    typeBox.setTooltip ("Filter type");
    slopeBox.setTooltip ("Slope (high / low pass)");
    placementBox.setTooltip ("Stereo placement: both channels, left, right, mid or side");
    bypassButton.setTooltip ("Bypass this band");
    soloButton.setTooltip ("Audition only the frequency region of this band");
    deleteButton.setTooltip ("Remove this band");

    for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &slopeBox, &placementBox, &freqKnob, &gainKnob, &qKnob,
                                                            &bypassButton, &soloButton, &deleteButton, &inputKnob, &outputKnob })
        addChildComponent (c);

    inputKnob.setVisible (true);
    outputKnob.setVisible (true);

    bypassButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, colours::warning.withAlpha (0.22f));
    soloButton.setColour (juce::TextButton::textColourOnId, colours::warning);
    deleteButton.setColour (juce::TextButton::textColourOffId, colours::danger.withAlpha (0.9f));

    soloButton.onClick = [this]
    {
        if (boundBand < 0)
            return;
        ctx.processor.soloBand = (ctx.processor.soloBand.load() == boundBand) ? -1 : boundBand;
    };

    deleteButton.onClick = [this]
    {
        if (boundBand >= 0)
            ctx.deleteBands ({ boundBand });
    };
}

void BandControlPanel::bindToBand (int band)
{
    using params::BandField;
    using params::bandParamID;

    typeAttachment.reset();
    slopeAttachment.reset();
    placementAttachment.reset();
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    bypassAttachment.reset();

    boundBand = band;
    const bool visible = band >= 0;

    for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &slopeBox, &placementBox, &freqKnob, &gainKnob, &qKnob,
                                                            &bypassButton, &soloButton, &deleteButton })
        c->setVisible (visible);

    if (! visible)
    {
        repaint();
        return;
    }

    auto& s = ctx.state;
    typeAttachment = std::make_unique<ComboAttachment> (s, bandParamID (band, BandField::Type), typeBox);
    slopeAttachment = std::make_unique<ComboAttachment> (s, bandParamID (band, BandField::Slope), slopeBox);
    placementAttachment = std::make_unique<ComboAttachment> (s, bandParamID (band, BandField::Placement), placementBox);
    freqAttachment = std::make_unique<SliderAttachment> (s, bandParamID (band, BandField::Frequency), freqKnob.slider);
    gainAttachment = std::make_unique<SliderAttachment> (s, bandParamID (band, BandField::Gain), gainKnob.slider);
    qAttachment = std::make_unique<SliderAttachment> (s, bandParamID (band, BandField::Q), qKnob.slider);
    bypassAttachment = std::make_unique<ButtonAttachment> (s, bandParamID (band, BandField::Bypass), bypassButton);

    const auto colour = colours::forBand (band);
    for (auto* k : { &freqKnob, &gainKnob, &qKnob })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);

    freqKnob.slider.setDoubleClickReturnValue (true, 1000.0);
    gainKnob.slider.setDoubleClickReturnValue (true, 0.0);
    qKnob.slider.setDoubleClickReturnValue (true, 0.7071);

    repaint();
}

void BandControlPanel::refresh()
{
    const int primary = ctx.primaryBand();
    if (primary != boundBand)
        bindToBand (primary);

    if (boundBand < 0)
        return;

    const auto p = ctx.readBand (boundBand);
    gainKnob.setEnabled (dsp::typeUsesGain (p.type));
    qKnob.setEnabled (dsp::typeUsesQ (p.type, p.slopeIndex));
    slopeBox.setEnabled (dsp::typeUsesSlope (p.type));
    soloButton.setToggleState (ctx.processor.soloBand.load() == boundBand, juce::dontSendNotification);
}

void BandControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawVerticalLine (globalArea.getX() - 1, 8.0f, (float) getHeight() - 8.0f);

    if (boundBand < 0)
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText ("Double-click the display to add a band. Select a band to edit it here.",
                    bandArea, juce::Justification::centred, true);
        return;
    }

    // Band identity
    auto id = bandArea.withWidth (120).reduced (12, 0).withTrimmedTop (14).withHeight (24);
    g.setColour (colours::forBand (boundBand));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ (float) id.getX() + 5.0f, (float) id.getCentreY() }));
    g.setColour (colours::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText ("Band " + juce::String (boundBand + 1), id.withTrimmedLeft (16), juce::Justification::centredLeft, false);
}

void BandControlPanel::resized()
{
    auto area = getLocalBounds();
    globalArea = area.removeFromRight (190);
    bandArea = area;

    auto g = globalArea.reduced (12, 10);
    inputKnob.setBounds (g.removeFromLeft (g.getWidth() / 2).reduced (4, 0));
    outputKnob.setBounds (g.reduced (4, 0));

    auto b = bandArea.reduced (8, 10);

    auto idCol = b.removeFromLeft (124);
    typeBox.setBounds (idCol.withTrimmedTop (44).withHeight (26).reduced (4, 0));

    const int knobW = juce::jlimit (78, 110, b.getWidth() / 7);
    freqKnob.setBounds (b.removeFromLeft (knobW).reduced (4, 0));
    gainKnob.setBounds (b.removeFromLeft (knobW).reduced (4, 0));
    qKnob.setBounds (b.removeFromLeft (knobW).reduced (4, 0));

    b.removeFromLeft (10);
    auto combos = b.removeFromLeft (150);
    const int rowH = 26;
    const int top = combos.getY() + (combos.getHeight() - (rowH * 2 + 8)) / 2;
    slopeBox.setBounds (combos.getX(), top, combos.getWidth(), rowH);
    placementBox.setBounds (combos.getX(), top + rowH + 8, combos.getWidth(), rowH);

    b.removeFromLeft (14);
    auto buttons = b.removeFromLeft (90);
    const int btnH = 24;
    const int btnTop = buttons.getY() + (buttons.getHeight() - (btnH * 3 + 12)) / 2;
    bypassButton.setBounds (buttons.getX(), btnTop, buttons.getWidth(), btnH);
    soloButton.setBounds (buttons.getX(), btnTop + btnH + 6, buttons.getWidth(), btnH);
    deleteButton.setBounds (buttons.getX(), btnTop + 2 * (btnH + 6), buttons.getWidth(), btnH);
}

} // namespace eyra::ui
