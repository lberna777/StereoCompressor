#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

// ── Componente custom: spettro input + filter response + GR overlay ──
class FreqResponseDisplay : public juce::Component,
                             private juce::Timer
{
public:
    explicit FreqResponseDisplay(StereoCompressorProcessor& p);
    ~FreqResponseDisplay() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    StereoCompressorProcessor& processor;

    // FFT plumbing
    juce::dsp::FFT fft { StereoCompressorProcessor::kFFTOrder };
    juce::dsp::WindowingFunction<float> window {
        (size_t) StereoCompressorProcessor::kFFTSize,
        juce::dsp::WindowingFunction<float>::hann };
    std::array<float, StereoCompressorProcessor::kFFTSize * 2> fftWork {};

    static constexpr int kBins = 160;
    std::array<float, kBins> spectrumDB {};

    float displayedGR { 0.0f };
};

// ── Meter verticale stereo (I o O) ──
class VerticalMeter : public juce::Component,
                       private juce::Timer
{
public:
    enum Side { Input, Output };

    VerticalMeter(StereoCompressorProcessor& p, Side s);
    ~VerticalMeter() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    StereoCompressorProcessor& processor;
    Side side;
    float displayedL { -90.0f };
    float displayedR { -90.0f };
    float peakHoldL  { -90.0f };
    float peakHoldR  { -90.0f };
    int   peakHoldCountL { 0 };
    int   peakHoldCountR { 0 };
};

// ── Editor principale ──
class StereoCompressorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit StereoCompressorEditor(StereoCompressorProcessor&);
    ~StereoCompressorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintTentacleIcon(juce::Graphics& g, juce::Rectangle<int> bounds, float intensity);

    StereoCompressorProcessor& processor;
    NeomodernLookAndFeel lnf;

    struct KnobGroup
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    KnobGroup hpFreq, lpFreq;
    KnobGroup threshold, attack, release, makeup;
    KnobGroup habisso, width;

    // Ratio 1176-style
    juce::TextButton ratioButtons[4];
    juce::Label      ratioLabel;

    // Display + meter
    FreqResponseDisplay freqDisplay;
    VerticalMeter       inMeter;
    VerticalMeter       outMeter;

    // Per evitare repaint inutili del tentacolo
    float lastHabissoVisual { -1.0f };

    // Rect dove disegnare il tentacolo (calcolato in resized())
    juce::Rectangle<int> tentacleArea;

    void setupKnob(KnobGroup& g, const juce::String& paramID, const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoCompressorEditor)
};
