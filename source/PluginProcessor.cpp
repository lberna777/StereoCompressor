#include "PluginProcessor.h"
#include "PluginEditor.h"

StereoCompressorProcessor::StereoCompressorProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int ch = 0; ch < 2; ++ch)
    {
        inputLevels[ch].store(-90.0f);
        outputLevels[ch].store(-90.0f);
    }
    std::fill(fftFifo.begin(),    fftFifo.end(),    0.0f);
    std::fill(fftPending.begin(), fftPending.end(), 0.0f);
}

void StereoCompressorProcessor::pushSampleToFFTFifo(float sample)
{
    if (fftFifoIdx >= kFFTSize)
    {
        // Solo se l'editor ha già consumato il blocco precedente
        if (! fftBlockReady.load(std::memory_order_acquire))
        {
            std::copy(fftFifo.begin(), fftFifo.end(), fftPending.begin());
            fftBlockReady.store(true, std::memory_order_release);
        }
        fftFifoIdx = 0;
    }
    fftFifo[(size_t) fftFifoIdx++] = sample;
}

bool StereoCompressorProcessor::consumeFFTBlock(float* dst)
{
    if (! fftBlockReady.load(std::memory_order_acquire))
        return false;
    std::copy(fftPending.begin(), fftPending.end(), dst);
    fftBlockReady.store(false, std::memory_order_release);
    return true;
}

float StereoCompressorProcessor::ratioFromIndex(int idx)
{
    switch (idx)
    {
        case 0: return 4.0f;
        case 1: return 8.0f;
        case 2: return 12.0f;
        case 3: return 20.0f;
        default: return 4.0f;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
StereoCompressorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ── EQ filtri ──
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpFreq", "Hi-Pass",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lpFreq", "Lo-Pass",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // ── Compressore ──
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "ratioSel", "Ratio",
        juce::StringArray { "4:1", "8:1", "12:1", "20:1" },
        0)); // default 4:1

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float>(0.1f, 200.0f, 0.1f, 0.5f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.5f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "makeup", "Makeup",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // ── HABISSO saturation ──
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "habisso", "Habisso",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // ── Stereo widener ──
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "width", "Width",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.3f));

    return { params.begin(), params.end() };
}

void StereoCompressorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    envDB = 0.0f;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 1; // un filtro mono per canale

    for (int ch = 0; ch < 2; ++ch)
    {
        hpFilter[ch].prepare(spec);
        hpFilter[ch].reset();
        lpFilter[ch].prepare(spec);
        lpFilter[ch].reset();
    }

    const double rampSec = 0.05;
    hpFreqSmoothed.reset(sampleRate, rampSec);
    lpFreqSmoothed.reset(sampleRate, rampSec);
    habissoSmoothed.reset(sampleRate, rampSec);

    hpFreqSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue("hpFreq")->load());
    lpFreqSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue("lpFreq")->load());
    habissoSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue("habisso")->load());

    lastHpFreq = -1.0f; // forza ricalcolo coeff. al primo sample
    lastLpFreq = -1.0f;
}

void StereoCompressorProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // ── Misura livello input (peak per blocco) + push FFT FIFO ──
    {
        float pL = 0.0f, pR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            pL = std::max(pL, std::abs(l));
            pR = std::max(pR, std::abs(r));
            // Mono sum per spettro (input pre-filtri)
            pushSampleToFFTFifo(0.5f * (l + r));
        }
        inputLevels[0].store(juce::Decibels::gainToDecibels(pL, -90.0f));
        inputLevels[1].store(juce::Decibels::gainToDecibels(pR, -90.0f));
    }

    // ── Leggi parametri (una volta per blocco) ──
    const float threshold = apvts.getRawParameterValue("threshold")->load();
    const int   ratioIdx  = (int) apvts.getRawParameterValue("ratioSel")->load();
    const float ratio     = ratioFromIndex(ratioIdx);
    const float attackMs  = apvts.getRawParameterValue("attack")->load();
    const float releaseMs = apvts.getRawParameterValue("release")->load();
    const float makeup    = apvts.getRawParameterValue("makeup")->load();
    const float width     = apvts.getRawParameterValue("width")->load();

    hpFreqSmoothed .setTargetValue(apvts.getRawParameterValue("hpFreq" )->load());
    lpFreqSmoothed .setTargetValue(apvts.getRawParameterValue("lpFreq" )->load());
    habissoSmoothed.setTargetValue(apvts.getRawParameterValue("habisso")->load());

    attackCoeff  = std::exp(-1.0f / (float(currentSampleRate) * attackMs  * 0.001f));
    releaseCoeff = std::exp(-1.0f / (float(currentSampleRate) * releaseMs * 0.001f));

    float grAccum = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // ── Update coefficienti HP/LP se la freq (smoothed) è cambiata ──
        const float hpF = hpFreqSmoothed.getNextValue();
        const float lpF = lpFreqSmoothed.getNextValue();

        if (std::abs(hpF - lastHpFreq) > 1.0f)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass(
                currentSampleRate, juce::jlimit(20.0f, 500.0f, hpF));
            hpFilter[0].coefficients = c;
            hpFilter[1].coefficients = c;
            lastHpFreq = hpF;
        }
        if (std::abs(lpF - lastLpFreq) > 1.0f)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass(
                currentSampleRate, juce::jlimit(2000.0f, 20000.0f, lpF));
            lpFilter[0].coefficients = c;
            lpFilter[1].coefficients = c;
            lastLpFreq = lpF;
        }

        // ── 1. HP filter ──
        left [i] = hpFilter[0].processSample(left [i]);
        right[i] = hpFilter[1].processSample(right[i]);

        // ── 2. LP filter ──
        left [i] = lpFilter[0].processSample(left [i]);
        right[i] = lpFilter[1].processSample(right[i]);

        // ── 3. COMPRESSORE peak-detector stereo-linked ──
        const float peak   = std::max(std::abs(left[i]), std::abs(right[i]));
        const float peakDB = (peak > 1e-6f) ? juce::Decibels::gainToDecibels(peak) : -120.0f;

        float gainDB = 0.0f;
        if (peakDB > threshold)
            gainDB = (threshold - peakDB) * (1.0f - 1.0f / ratio);

        if (gainDB < envDB)
            envDB = attackCoeff  * envDB + (1.0f - attackCoeff)  * gainDB;
        else
            envDB = releaseCoeff * envDB + (1.0f - releaseCoeff) * gainDB;

        const float gain = juce::Decibels::decibelsToGain(envDB + makeup);
        left [i] *= gain;
        right[i] *= gain;

        grAccum += envDB;

        // ── 4. HABISSO — waveshaper tipo tape (tanh saturation) ──
        const float hab01 = habissoSmoothed.getNextValue() * 0.01f; // 0..1
        if (hab01 > 0.001f)
        {
            const float drive = 1.0f + hab01 * 6.0f;
            const float norm  = std::tanh(drive);
            left [i] = std::tanh(left [i] * drive) / norm;
            right[i] = std::tanh(right[i] * drive) / norm;
            // Leggera compensazione di livello (tanh aumenta l'energia percepita)
            const float comp = 1.0f - hab01 * 0.18f;
            left [i] *= comp;
            right[i] *= comp;
        }

        // ── 5. STEREO WIDENER (M/S) ──
        const float mid  = (left[i] + right[i]) * 0.5f;
        float       side = (left[i] - right[i]) * 0.5f;
        side *= width;
        left [i] = mid + side;
        right[i] = mid - side;
    }

    currentGR.store(grAccum / float(numSamples));
    displayedHp.store(hpFreqSmoothed.getCurrentValue());
    displayedLp.store(lpFreqSmoothed.getCurrentValue());

    // ── Misura livello output ──
    {
        float pL = 0.0f, pR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            pL = std::max(pL, std::abs(left[i]));
            pR = std::max(pR, std::abs(right[i]));
        }
        outputLevels[0].store(juce::Decibels::gainToDecibels(pL, -90.0f));
        outputLevels[1].store(juce::Decibels::gainToDecibels(pR, -90.0f));
    }
}

void StereoCompressorProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void StereoCompressorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StereoCompressorProcessor();
}
