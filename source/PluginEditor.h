#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

// ── FreqResponseDisplay ─────────────────────────────────────────────────────
class FreqResponseDisplay : public juce::Component, private juce::Timer
{
public:
    explicit FreqResponseDisplay(StereoCompressorProcessor& p);
    ~FreqResponseDisplay() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    StereoCompressorProcessor& processor;

    juce::dsp::FFT fft { StereoCompressorProcessor::kFFTOrder };
    juce::dsp::WindowingFunction<float> window {
        (size_t) StereoCompressorProcessor::kFFTSize,
        juce::dsp::WindowingFunction<float>::hann };
    std::array<float, StereoCompressorProcessor::kFFTSize * 2> fftWork {};

    static constexpr int kBins = 160;
    std::array<float, kBins> spectrumDB {};
    float displayedGR { 0.0f };
};

// ── VerticalMeter ───────────────────────────────────────────────────────────
class VerticalMeter : public juce::Component, private juce::Timer
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

// ── OctopusDisplay ──────────────────────────────────────────────────────────
class OctopusDisplay : public juce::Component
{
public:
    explicit OctopusDisplay(StereoCompressorProcessor& p);

    // Chiamata dal timerCallback() dell'editor ogni 30 Hz
    void update(float eqAggr, float widthNorm, float comprNorm, float habNorm);

    void paint(juce::Graphics&) override;

private:
    float smoothEqAggr    { 0.0f };
    float smoothWidthNorm { 0.5f };
    float smoothCompr     { 0.0f };
    float smoothHab       { 0.0f };
    float wavePhase       { 0.0f };

    void paintBackground   (juce::Graphics&);
    void paintWaterSurface (juce::Graphics&, float waterY);
    void paintOctopusHead  (juce::Graphics&, float cx, float headY, float compr);
    void paintTentacle     (juce::Graphics&, int index, float cx, float baseY,
                            float aggr, float waterY);
    void paintSuctionCups  (juce::Graphics&, const juce::Path& path, float aggr);

    StereoCompressorProcessor& processor;
};

// ── StereoCompressorEditor ──────────────────────────────────────────────────
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
    void updateOctopusParameters();
    void paintParalarvaIcon(juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            float paralarvaVal);

    StereoCompressorProcessor& processor;
    NeomodernLookAndFeel lnf;

    struct KnobGroup
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    KnobGroup hpFreq, hpQ;
    KnobGroup lpFreq, lpQ;
    KnobGroup threshold, ratio, attack, release, makeup;
    KnobGroup habiss, paralarva;

    FreqResponseDisplay freqDisplay;
    VerticalMeter       inMeter;
    VerticalMeter       outMeter;
    OctopusDisplay      octopusDisplay;

    juce::Rectangle<int> paralarvaIconArea;

    void setupKnob(KnobGroup& g, const juce::String& paramID, const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoCompressorEditor)
};
