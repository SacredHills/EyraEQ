#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace eyra::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff0b0d10 };
    inline const juce::Colour panel { 0xff111419 };
    inline const juce::Colour panelRaised { 0xff171b21 };
    inline const juce::Colour control { 0xff1d222a };
    inline const juce::Colour outline { 0xff272d37 };
    inline const juce::Colour grid { 0xff1a1f26 };
    inline const juce::Colour gridStrong { 0xff252b34 };
    inline const juce::Colour text { 0xffd3d9e0 };
    inline const juce::Colour textDim { 0xff7c8591 };
    inline const juce::Colour textFaint { 0xff4c5561 };
    inline const juce::Colour accent { 0xff3fc1b0 };   // Eyra teal
    inline const juce::Colour warning { 0xffe8a33d };
    inline const juce::Colour danger { 0xffe0645a };
    inline const juce::Colour curve { 0xffe9eef2 };
    inline const juce::Colour spectrumPre { 0xff2b3442 };
    inline const juce::Colour spectrumPost { 0xff6d86a8 };

    /** A distinct colour for each band slot. */
    juce::Colour forBand (int bandIndex);
} // namespace colours

juce::Font uiFont (float height, bool bold = false);

class EyraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EyraLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace eyra::ui
