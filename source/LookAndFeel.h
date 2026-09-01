#pragma once
#include <JuceHeader.h>

class NeomodernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeomodernLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont(juce::Label&) override;

    // ── Palette dark/trap ──
    static const juce::Colour PANEL_LIGHT;
    static const juce::Colour PANEL_LIGHT_HI;
    static const juce::Colour PANEL_DARK;
    static const juce::Colour ACCENT_CYAN;
    static const juce::Colour ACCENT_CYAN_DIM;
    static const juce::Colour KNOB_BODY;
    static const juce::Colour KNOB_BODY_HI;
    static const juce::Colour KNOB_RIM;
    static const juce::Colour TEXT_DARK;
    static const juce::Colour TEXT_MUTED;
    static const juce::Colour METER_GREEN;
    static const juce::Colour METER_YELLOW;
    static const juce::Colour METER_RED;
    static const juce::Colour OCEAN_DEEP;
    static const juce::Colour WATER_SURFACE;
};
