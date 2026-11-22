#include "EyraLookAndFeel.h"

namespace eyra::ui
{

juce::Colour colours::forBand (int bandIndex)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xff5ec8f2), juce::Colour (0xff7ee0a8), juce::Colour (0xfff2c45e), juce::Colour (0xfff28c6b),
        juce::Colour (0xffb79cf2), juce::Colour (0xfff27eb3), juce::Colour (0xff6bd6d0), juce::Colour (0xffc8e07e),
    };
    return palette[(size_t) (bandIndex % 8)];
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

EyraLookAndFeel::EyraLookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (DocumentWindow::backgroundColourId, colours::background);

    setColour (TextButton::buttonColourId, colours::control);
    setColour (TextButton::buttonOnColourId, colours::accent.withAlpha (0.22f));
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::accent);

    setColour (ComboBox::backgroundColourId, colours::control);
    setColour (ComboBox::outlineColourId, colours::outline);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::arrowColourId, colours::textDim);

    setColour (PopupMenu::backgroundColourId, colours::panelRaised);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.18f));
    setColour (PopupMenu::highlightedTextColourId, colours::text);
    setColour (PopupMenu::headerTextColourId, colours::textDim);

    setColour (Label::textColourId, colours::text);
    setColour (Label::textWhenEditingColourId, colours::text);
    setColour (Label::backgroundWhenEditingColourId, colours::control);
    setColour (Label::outlineWhenEditingColourId, colours::accent);

    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.35f));
    setColour (Slider::rotarySliderFillColourId, colours::accent);
    setColour (Slider::rotarySliderOutlineColourId, colours::outline);

    setColour (TextEditor::backgroundColourId, colours::control);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, colours::outline);
    setColour (TextEditor::focusedOutlineColourId, colours::accent);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, colours::accent);

    setColour (AlertWindow::backgroundColourId, colours::panelRaised);
    setColour (AlertWindow::textColourId, colours::text);
    setColour (AlertWindow::outlineColourId, colours::outline);

    setColour (TooltipWindow::backgroundColourId, colours::panelRaised);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::outline);
}

void EyraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = jmax (2.0f, radius * 0.12f);
    const bool enabled = slider.isEnabled();
    const auto fill = slider.findColour (Slider::rotarySliderFillColourId).withAlpha (enabled ? 1.0f : 0.25f);

    // Track
    Path track;
    track.addCentredArc (centre.x, centre.y, radius - trackWidth, radius - trackWidth, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded));

    // Value arc: bipolar sliders (range symmetric around 0) draw from the centre.
    const auto range = slider.getRange();
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zeroPos * (endAngle - startAngle);
    const float a1 = startAngle + pos * (endAngle - startAngle);

    Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - trackWidth, radius - trackWidth, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
    g.setColour (fill);
    g.strokePath (arc, PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded));

    // Knob body
    const float bodyRadius = radius - trackWidth * 2.6f;
    g.setColour (colours::control.brighter (slider.isMouseOverOrDragging() && enabled ? 0.12f : 0.0f));
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (colours::outline);
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    // Pointer
    const float pointerLength = bodyRadius * 0.62f;
    Path pointer;
    pointer.addRoundedRectangle (-1.25f, -bodyRadius + 3.0f, 2.5f, pointerLength, 1.25f);
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.fillPath (pointer, AffineTransform::rotation (a1).translated (centre));
}

void EyraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool isMouseOver, bool isDown)
{
    using namespace juce;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    Colour fill = on ? button.findColour (TextButton::buttonOnColourId) : button.findColour (TextButton::buttonColourId);
    if (isDown)
        fill = fill.brighter (0.15f);
    else if (isMouseOver && button.isEnabled())
        fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (on ? button.findColour (TextButton::textColourOnId).withAlpha (0.5f) : colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void EyraLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    using namespace juce;
    g.setFont (getTextButtonFont (button, button.getHeight()));
    auto colour = button.findColour (button.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId);
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.35f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), Justification::centred, 1);
}

juce::Font EyraLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (14.0f, (float) buttonHeight * 0.52f));
}

void EyraLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    auto bounds = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId).brighter (box.isMouseOver() && box.isEnabled() ? 0.06f : 0.0f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent.withAlpha (0.6f) : box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const float arrowX = (float) width - 14.0f;
    const float cy = (float) height * 0.5f;
    Path arrow;
    arrow.startNewSubPath (arrowX - 4.0f, cy - 2.0f);
    arrow.lineTo (arrowX, cy + 2.0f);
    arrow.lineTo (arrowX + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.3f));
    g.strokePath (arrow, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

juce::Font EyraLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return uiFont (juce::jmin (14.0f, (float) box.getHeight() * 0.5f));
}

void EyraLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font EyraLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.5f);
}

void EyraLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::outline);
    g.drawRect (0, 0, width, height);
}

juce::Font EyraLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Slider::SliderLayout EyraLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();
    const int textHeight = slider.getTextBoxHeight();

    if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        layout.textBoxBounds = bounds.removeFromBottom (textHeight);
        layout.sliderBounds = bounds;
    }
    else
    {
        layout.sliderBounds = bounds;
    }
    return layout;
}

juce::Label* EyraLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (uiFont (13.0f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void EyraLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    g.setColour (colours::outline);
    g.drawRect (0, 0, width, height);
    g.setColour (colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (6, 2), juce::Justification::centredLeft, 3);
}

} // namespace eyra::ui
