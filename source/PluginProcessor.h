#pragma once
#include <JuceHeader.h>

class StereoCompressorProcessor : public juce::AudioProcessor
{
public:
    StereoCompressorProcessor();
    ~StereoCompressorProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NEMO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Metering / display getters (lock-free, letti dall'editor) ──
    float  getGainReductionDB()       const { return currentGR.load(); }
    float  getInputLevelDB (int ch)   const { return inputLevels[ch].load(); }
    float  getOutputLevelDB(int ch)   const { return outputLevels[ch].load(); }
    float  getCurrentHpFreq()         const { return displayedHp.load(); }
    float  getCurrentLpFreq()         const { return displayedLp.load(); }
    double getCurrentSampleRate()     const { return currentSampleRate; }

    // Coefficient builders pubblici (usati anche da FreqResponseDisplay)
    static juce::dsp::IIR::Coefficients<float>::Ptr
        makeHighPassWithQ(double sr, float freq, float Q);
    static juce::dsp::IIR::Coefficients<float>::Ptr
        makeLowPassWithQ (double sr, float freq, float Q);

    // ── FFT FIFO (input pre-filtri, consumato dall'editor) ──
    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize  = 1 << kFFTOrder;   // 2048

    bool consumeFFTBlock(float* dst);

private:
    void pushSampleToFFTFifo(float sample);

    std::array<float, kFFTSize> fftFifo {};
    std::array<float, kFFTSize> fftPending {};
    int  fftFifoIdx { 0 };
    std::atomic<bool> fftBlockReady { false };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    float envDB { 0.0f };
    float attackCoeff  { 0.0f };
    float releaseCoeff { 0.0f };
    double currentSampleRate { 44100.0 };

    juce::dsp::IIR::Filter<float> hpFilter[2];
    juce::dsp::IIR::Filter<float> lpFilter[2];
    float lastHpFreq { -1.0f };
    float lastLpFreq { -1.0f };
    float lastHpQ    {  0.0f };
    float lastLpQ    {  0.0f };

    juce::SmoothedValue<float> hpFreqSmoothed;
    juce::SmoothedValue<float> lpFreqSmoothed;
    juce::SmoothedValue<float> habissSmoothed;

    std::atomic<float> currentGR     { 0.0f };
    std::atomic<float> inputLevels[2];
    std::atomic<float> outputLevels[2];
    std::atomic<float> displayedHp   { 20.0f };
    std::atomic<float> displayedLp   { 20000.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoCompressorProcessor)
};
