#include "LookAndFeel.h"

// ── Palette dark/trap ──────────────────────────────────────────────────────
const juce::Colour NeomodernLookAndFeel::PANEL_LIGHT    { 0xff141820 };
const juce::Colour NeomodernLookAndFeel::PANEL_LIGHT_HI { 0xff1e2430 };
const juce::Colour NeomodernLookAndFeel::PANEL_DARK     { 0xff0a0d12 };
const juce::Colour NeomodernLookAndFeel::ACCENT_CYAN    { 0xff00e5c8 };
const juce::Colour NeomodernLookAndFeel::ACCENT_CYAN_DIM{ 0xff007a6a };
const juce::Colour NeomodernLookAndFeel::KNOB_BODY      { 0xff0c1018 };
const juce::Colour NeomodernLookAndFeel::KNOB_BODY_HI   { 0xff1a2230 };
const juce::Colour NeomodernLookAndFeel::KNOB_RIM       { 0xff243040 };
const juce::Colour NeomodernLookAndFeel::TEXT_DARK      { 0xffe0ecea };
const juce::Colour NeomodernLookAndFeel::TEXT_MUTED     { 0xff4a6a68 };
const juce::Colour NeomodernLookAndFeel::METER_GREEN    { 0xff00e5c8 };
const juce::Colour NeomodernLookAndFeel::METER_YELLOW   { 0xffffb020 };
const juce::Colour NeomodernLookAndFeel::METER_RED      { 0xffff2a4a };
const juce::Colour NeomodernLookAndFeel::OCEAN_DEEP     { 0xff060810 };
const juce::Colour NeomodernLookAndFeel::WATER_SURFACE  { 0xff1a4a5a };

NeomodernLookAndFeel::NeomodernLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId,       TEXT_DARK);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId,               TEXT_MUTED);
}

juce::Font NeomodernLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(10.5f, juce::Font::bold);
}

void NeomodernLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                              int x, int y, int width, int height,
                                              float sliderPos,
                                              float rotaryStartAngle,
                                              float rotaryEndAngle,
                                              juce::Slider&)
{
    const float cx = (float)x + (float)width  * 0.5f;
    const float cy = (float)y + (float)height * 0.5f;

    // P19 Igloo style: large outer arc, smaller knob body
    const float outerR = (float)juce::jmin(width, height) * 0.46f;
    const float bodyR  = outerR * 0.58f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // ── Arc track (sfondo, ferro di cavallo) ──
    {
        juce::Path bgArc;
        bgArc.addCentredArc(cx, cy, outerR, outerR, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(KNOB_RIM.withAlpha(0.5f));
        g.strokePath(bgArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ── Arc fill (valore corrente, ciano neon) ──
    {
        juce::Path fgArc;
        fgArc.addCentredArc(cx, cy, outerR, outerR, 0.0f,
                            rotaryStartAngle, angle, true);
        g.setColour(ACCENT_CYAN);
        g.strokePath(fgArc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ── Rim metallico intorno al corpo ──
    {
        juce::ColourGradient rimGrad(KNOB_RIM.brighter(0.2f), cx, cy - bodyR - 3,
                                      KNOB_RIM.darker(0.3f),  cx, cy + bodyR + 3, false);
        g.setGradientFill(rimGrad);
        g.fillEllipse(cx - bodyR - 2, cy - bodyR - 2, (bodyR + 2) * 2, (bodyR + 2) * 2);
    }

    // ── Drop shadow ──
    {
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillEllipse(cx - bodyR + 1, cy - bodyR + 3, bodyR * 2, bodyR * 2);
    }

    // ── Corpo del knob ──
    {
        juce::ColourGradient bodyGrad(KNOB_BODY_HI, cx, cy - bodyR * 0.8f,
                                       KNOB_BODY,   cx, cy + bodyR * 0.9f, false);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
    }

    // ── Highlight ──
    {
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        const float hlW = bodyR * 1.2f;
        const float hlH = bodyR * 0.5f;
        g.fillEllipse(cx - hlW * 0.5f, cy - bodyR * 0.90f, hlW, hlH);
    }

    // ── Indicatore (linea ciano) ──
    {
        const float ptrLen = bodyR * 0.60f;
        const float ptrW   = 2.5f;
        juce::Path ptr;
        ptr.addRoundedRectangle(-ptrW * 0.5f, -bodyR + 5.0f, ptrW, ptrLen, 1.2f);
        ptr.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
        g.setColour(ACCENT_CYAN);
        g.fillPath(ptr);
    }
}

void NeomodernLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour&,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = button.getToggleState() || shouldDrawButtonAsDown;

    juce::Colour base = on ? ACCENT_CYAN_DIM : KNOB_BODY;
    if (shouldDrawButtonAsHighlighted && !on) base = base.brighter(0.18f);

    juce::ColourGradient grad(base.brighter(0.32f), bounds.getCentreX(), bounds.getY(),
                               base.darker(0.25f),  bounds.getCentreX(), bounds.getBottom(),
                               false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(on ? ACCENT_CYAN.brighter(0.2f) : KNOB_RIM.darker(0.2f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    if (on)
    {
        g.setColour(ACCENT_CYAN.withAlpha(0.55f));
        g.drawRoundedRectangle(bounds.reduced(1.8f), 3.0f, 1.0f);
    }
}

void NeomodernLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                            bool, bool shouldDrawButtonAsDown)
{
    const bool on = button.getToggleState() || shouldDrawButtonAsDown;
    g.setColour(on ? juce::Colours::white : TEXT_MUTED);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}
