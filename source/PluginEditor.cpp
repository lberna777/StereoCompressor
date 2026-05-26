#include "PluginEditor.h"

// ═════════════════════════════════════════════════════════════════════
//   FreqResponseDisplay
// ═════════════════════════════════════════════════════════════════════

FreqResponseDisplay::FreqResponseDisplay(StereoCompressorProcessor& p)
    : processor(p)
{
    std::fill(spectrumDB.begin(), spectrumDB.end(), -90.0f);
    startTimerHz(30);
}

FreqResponseDisplay::~FreqResponseDisplay() { stopTimer(); }

void FreqResponseDisplay::timerCallback()
{
    // GR smoothing (decay lento, attack istantaneo)
    const float gr = processor.getGainReductionDB();
    if (gr < displayedGR) displayedGR = gr;
    else                  displayedGR = displayedGR * 0.85f + gr * 0.15f;

    const double sr = processor.getCurrentSampleRate();
    constexpr int N = StereoCompressorProcessor::kFFTSize;

    if (processor.consumeFFTBlock(fftWork.data()))
    {
        std::fill(fftWork.begin() + N, fftWork.end(), 0.0f);
        window.multiplyWithWindowingTable(fftWork.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform(fftWork.data());

        // Mappa bin FFT lineari → kBins su scala log (20 Hz → 20 kHz)
        for (int b = 0; b < kBins; ++b)
        {
            const float t = (float) b / (float) (kBins - 1);
            const float freq = 20.0f * std::pow(1000.0f, t);
            float binF = freq / (float)(sr * 0.5) * (float)(N / 2);
            binF = juce::jlimit(0.0f, (float)(N / 2 - 1), binF);
            const int  bi = (int) binF;

            const float mag  = fftWork[(size_t) bi] / (float)(N / 2);
            const float dB   = juce::Decibels::gainToDecibels(mag, -90.0f);

            // Fast attack, slow decay sul display
            if (dB > spectrumDB[(size_t) b]) spectrumDB[(size_t) b] = dB;
            else                              spectrumDB[(size_t) b] =
                spectrumDB[(size_t) b] * 0.82f + dB * 0.18f;
        }
    }
    else
    {
        // Niente nuovo blocco: decay lento verso il silenzio
        for (auto& v : spectrumDB)
            v = v * 0.95f - 90.0f * 0.05f;
    }

    repaint();
}

void FreqResponseDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // ── Background del display (dark recessed) ──
    {
        juce::ColourGradient bgGrad(NeomodernLookAndFeel::PANEL_DARK.darker(0.15f),
                                     bounds.getCentreX(), bounds.getY(),
                                     NeomodernLookAndFeel::PANEL_DARK,
                                     bounds.getCentreX(), bounds.getBottom(),
                                     false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    }

    auto plotArea  = bounds.reduced(8.0f);
    auto grBarArea = plotArea.removeFromBottom(8.0f);
    plotArea.removeFromBottom(4.0f);

    // Mapping helpers
    const float xMin   = plotArea.getX();
    const float xW     = plotArea.getWidth();
    const float yTop   = plotArea.getY();
    const float yBot   = plotArea.getBottom();
    const float yH     = plotArea.getHeight();

    // dB → y. Visualizziamo da +6 dB a -42 dB (range 48 dB) per accogliere
    // sia la curva del filtro sia lo spettro.
    constexpr float DB_TOP = 6.0f;
    constexpr float DB_BOT = -42.0f;
    auto dBToY = [&](float dB)
    {
        float t = (dB - DB_BOT) / (DB_TOP - DB_BOT);
        return yBot - juce::jlimit(0.0f, 1.0f, t) * yH;
    };
    auto freqToX = [&](float f)
    {
        float t = std::log10(f / 20.0f) / std::log10(20000.0f / 20.0f);
        return xMin + t * xW;
    };

    // ── Grid ──
    {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        for (int f : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
            g.drawVerticalLine((int) freqToX((float) f), yTop, yBot);

        for (int dB : { 0, -12, -24, -36 })
            g.drawHorizontalLine((int) dBToY((float) dB), xMin, plotArea.getRight());

        g.setColour(juce::Colours::white.withAlpha(0.14f));
        g.drawHorizontalLine((int) dBToY(0.0f), xMin, plotArea.getRight());

        g.setColour(NeomodernLookAndFeel::TEXT_MUTED.withAlpha(0.6f));
        g.setFont(juce::Font(8.5f, juce::Font::plain));
        for (int f : { 100, 1000, 10000 })
        {
            juce::String lbl = (f >= 1000) ? juce::String(f / 1000) + "k" : juce::String(f);
            g.drawText(lbl, (int) freqToX((float) f) - 12, (int) yBot - 12,
                       24, 10, juce::Justification::centred);
        }
    }

    // ── 1. Spettro input (mountain) ──
    {
        juce::Path mountain;
        mountain.startNewSubPath(xMin, yBot);
        for (int b = 0; b < kBins; ++b)
        {
            float t = (float) b / (float) (kBins - 1);
            float freq = 20.0f * std::pow(1000.0f, t);
            float x = freqToX(freq);
            float dB = juce::jlimit(DB_BOT, DB_TOP, spectrumDB[(size_t) b] + 6.0f);
            mountain.lineTo(x, dBToY(dB));
        }
        mountain.lineTo(plotArea.getRight(), yBot);
        mountain.closeSubPath();

        juce::ColourGradient sg(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.55f),
                                 plotArea.getCentreX(), yTop,
                                 NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.06f),
                                 plotArea.getCentreX(), yBot, false);
        g.setGradientFill(sg);
        g.fillPath(mountain);

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.65f));
        g.strokePath(mountain, juce::PathStrokeType(1.0f));
    }

    // ── 2. Curva del filtro HP × LP (letta SEMPRE da APVTS → sempre reattiva) ──
    {
        const double sr = processor.getCurrentSampleRate();
        const float hpF = juce::jlimit(20.0f, 500.0f,
            processor.apvts.getRawParameterValue("hpFreq")->load());
        const float lpF = juce::jlimit(2000.0f, 20000.0f,
            processor.apvts.getRawParameterValue("lpFreq")->load());

        auto hpCoef = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, hpF);
        auto lpCoef = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, lpF);

        juce::Path curve;
        const int N = 240;
        for (int i = 0; i <= N; ++i)
        {
            float t = (float) i / (float) N;
            float freq = 20.0f * std::pow(1000.0f, t);
            double mag = hpCoef->getMagnitudeForFrequency((double) freq, sr)
                       * lpCoef->getMagnitudeForFrequency((double) freq, sr);
            float dB = (float) juce::Decibels::gainToDecibels(mag);
            float x  = freqToX(freq);
            float y  = dBToY(juce::jlimit(DB_BOT, DB_TOP, dB));
            if (i == 0) curve.startNewSubPath(x, y);
            else        curve.lineTo(x, y);
        }

        // Glow morbido sotto la linea principale
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.strokePath(curve, juce::PathStrokeType(4.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Linea principale: bianco brillante = sempre visibile sullo spettro
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.strokePath(curve, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Punti di taglio: marcatori a -3 dB sui due cutoff
        auto drawMarker = [&](float freq, juce::Colour c)
        {
            float x = freqToX(juce::jlimit(20.0f, 20000.0f, freq));
            float y = dBToY(-3.0f);
            g.setColour(c.withAlpha(0.95f));
            g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.drawEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.0f);
        };
        // Mostra il marker solo se il filtro è "in azione" (non a estremo bypass)
        if (hpF > 22.0f)    drawMarker(hpF, NeomodernLookAndFeel::ACCENT_CYAN);
        if (lpF < 19000.0f) drawMarker(lpF, NeomodernLookAndFeel::ACCENT_CYAN);
    }

    // ── 3. GR bar in basso ──
    {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(grBarArea, 2.0f);

        float grAbs  = juce::jlimit(0.0f, 20.0f, -displayedGR);
        float grNorm = grAbs / 20.0f;
        auto  fillR  = grBarArea.withWidth(grBarArea.getWidth() * grNorm);
        g.setColour(NeomodernLookAndFeel::METER_RED.withAlpha(0.85f));
        g.fillRoundedRectangle(fillR, 2.0f);

        g.setColour(NeomodernLookAndFeel::TEXT_MUTED);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText("GR  " + juce::String(displayedGR, 1) + " dB",
                   grBarArea.toNearestInt(),
                   juce::Justification::centredRight);
    }
}

// ═════════════════════════════════════════════════════════════════════
//   VerticalMeter
// ═════════════════════════════════════════════════════════════════════

VerticalMeter::VerticalMeter(StereoCompressorProcessor& p, Side s)
    : processor(p), side(s)
{
    startTimerHz(30);
}

VerticalMeter::~VerticalMeter() { stopTimer(); }

void VerticalMeter::timerCallback()
{
    const float tL = (side == Input) ? processor.getInputLevelDB(0) : processor.getOutputLevelDB(0);
    const float tR = (side == Input) ? processor.getInputLevelDB(1) : processor.getOutputLevelDB(1);

    // Fast attack, slow decay
    auto smooth = [](float& cur, float target)
    {
        if (target > cur) cur = target;
        else              cur = cur * 0.86f + target * 0.14f;
    };
    smooth(displayedL, tL);
    smooth(displayedR, tR);

    // Peak hold: si aggiorna a nuovo massimo, decade dopo ~1s (30 frame)
    auto updateHold = [](float lvl, float& hold, int& cnt)
    {
        if (lvl >= hold) { hold = lvl; cnt = 30; }
        else if (--cnt <= 0) { hold -= 1.5f; if (hold < -90.0f) hold = -90.0f; }
    };
    updateHold(displayedL, peakHoldL, peakHoldCountL);
    updateHold(displayedR, peakHoldR, peakHoldCountR);

    repaint();
}

void VerticalMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background scuro
    {
        juce::ColourGradient bgGrad(NeomodernLookAndFeel::PANEL_DARK.darker(0.1f),
                                     bounds.getCentreX(), bounds.getY(),
                                     NeomodernLookAndFeel::PANEL_DARK,
                                     bounds.getCentreX(), bounds.getBottom(),
                                     false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    }

    auto inner = bounds.reduced(4.0f);

    // Etichetta IN/OUT in basso
    auto labelArea = inner.removeFromBottom(14.0f);
    g.setColour(NeomodernLookAndFeel::TEXT_MUTED);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(side == Input ? "IN" : "OUT",
               labelArea.toNearestInt(),
               juce::Justification::centred);

    inner.removeFromBottom(2.0f);

    // Range del meter
    const float minDb = -60.0f, maxDb = 6.0f;
    auto dBToNorm = [&](float dB)
    {
        return juce::jmap(juce::jlimit(minDb, maxDb, dB), minDb, maxDb, 0.0f, 1.0f);
    };

    // Due barre L/R
    const float gap = 2.0f;
    const float barW = (inner.getWidth() - gap) * 0.5f;
    auto barL = juce::Rectangle<float>(inner.getX(),              inner.getY(), barW, inner.getHeight());
    auto barR = juce::Rectangle<float>(inner.getX() + barW + gap, inner.getY(), barW, inner.getHeight());

    auto drawBar = [&](juce::Rectangle<float> r, float dB, float hold)
    {
        // Track sottostante (per dare frame sempre visibile)
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(r, 1.5f);

        const float norm = dBToNorm(dB);
        if (norm > 0.001f)
        {
            auto fillR = r.withTop(r.getBottom() - r.getHeight() * norm);
            juce::ColourGradient grad(NeomodernLookAndFeel::METER_RED,
                                       fillR.getCentreX(), r.getY(),
                                       NeomodernLookAndFeel::METER_GREEN,
                                       fillR.getCentreX(), r.getBottom(), false);
            grad.addColour(0.55, NeomodernLookAndFeel::METER_YELLOW);
            g.setGradientFill(grad);
            g.fillRect(fillR);
        }

        // Peak-hold tick
        const float holdNorm = dBToNorm(hold);
        if (holdNorm > 0.001f)
        {
            const float y = r.getBottom() - r.getHeight() * holdNorm;
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.fillRect(juce::Rectangle<float>(r.getX(), y - 1.0f, r.getWidth(), 1.5f));
        }
    };

    drawBar(barL, displayedL, peakHoldL);
    drawBar(barR, displayedR, peakHoldR);

    // Tacche dB (-40, -20, -12, -6, -3, 0)
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    for (int dB : { -40, -20, -12, -6, -3, 0 })
    {
        const float y = inner.getBottom() - inner.getHeight() * dBToNorm((float) dB);
        g.drawHorizontalLine((int) y, inner.getX(), inner.getRight());
    }

    // Linea rossa a 0 dB
    g.setColour(NeomodernLookAndFeel::METER_RED.withAlpha(0.5f));
    const float y0 = inner.getBottom() - inner.getHeight() * dBToNorm(0.0f);
    g.drawHorizontalLine((int) y0, inner.getX(), inner.getRight());
}

// ═════════════════════════════════════════════════════════════════════
//   StereoCompressorEditor
// ═════════════════════════════════════════════════════════════════════

StereoCompressorEditor::StereoCompressorEditor(StereoCompressorProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      freqDisplay(p),
      inMeter(p, VerticalMeter::Input),
      outMeter(p, VerticalMeter::Output)
{
    setLookAndFeel(&lnf);
    setSize(760, 520);

    setupKnob(hpFreq,    "hpFreq",    "HI-PASS");
    setupKnob(lpFreq,    "lpFreq",    "LO-PASS");
    setupKnob(threshold, "threshold", "THRESHOLD");
    setupKnob(attack,    "attack",    "ATTACK");
    setupKnob(release,   "release",   "RELEASE");
    setupKnob(makeup,    "makeup",    "MAKEUP");
    setupKnob(habisso,   "habisso",   "HABISSO");
    setupKnob(width,     "width",     "WIDTH");

    addAndMakeVisible(freqDisplay);
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);

    // Ratio buttons (1176-style radio group)
    const char* btnLabels[] = { "4", "8", "12", "20" };
    for (int i = 0; i < 4; ++i)
    {
        ratioButtons[i].setButtonText(btnLabels[i]);
        ratioButtons[i].setClickingTogglesState(true);
        ratioButtons[i].setRadioGroupId(1001);
        addAndMakeVisible(ratioButtons[i]);

        ratioButtons[i].onClick = [this, i]
        {
            if (!ratioButtons[i].getToggleState())
                return;
            auto* param = processor.apvts.getParameter("ratioSel");
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1((float) i));
            param->endChangeGesture();
        };
    }
    int initialIdx = (int) processor.apvts.getRawParameterValue("ratioSel")->load();
    initialIdx = juce::jlimit(0, 3, initialIdx);
    ratioButtons[initialIdx].setToggleState(true, juce::dontSendNotification);

    ratioLabel.setText("RATIO", juce::dontSendNotification);
    ratioLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    ratioLabel.setColour(juce::Label::textColourId, NeomodernLookAndFeel::TEXT_MUTED);
    ratioLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ratioLabel);

    startTimerHz(30);
}

StereoCompressorEditor::~StereoCompressorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void StereoCompressorEditor::setupKnob(KnobGroup& g,
                                        const juce::String& paramID,
                                        const juce::String& name)
{
    g.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    g.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                  juce::MathConstants<float>::pi * 2.8f,
                                  true);
    g.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible(g.slider);

    g.label.setText(name, juce::dontSendNotification);
    g.label.setFont(juce::Font(10.0f, juce::Font::bold));
    g.label.setColour(juce::Label::textColourId, NeomodernLookAndFeel::TEXT_MUTED);
    g.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(g.label);

    g.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, g.slider);
}

void StereoCompressorEditor::timerCallback()
{
    // Sync ratio buttons col parametro (per preset load / automazione)
    int currentIdx = (int) processor.apvts.getRawParameterValue("ratioSel")->load();
    currentIdx = juce::jlimit(0, 3, currentIdx);
    if (! ratioButtons[currentIdx].getToggleState())
        ratioButtons[currentIdx].setToggleState(true, juce::dontSendNotification);

    // Repaint solo se habisso è cambiato visibilmente (per il tentacolo)
    const float hab = processor.apvts.getRawParameterValue("habisso")->load() * 0.01f;
    if (std::abs(hab - lastHabissoVisual) > 0.01f)
    {
        lastHabissoVisual = hab;
        repaint(tentacleArea);
    }
}

void StereoCompressorEditor::paintTentacleIcon(juce::Graphics& g,
                                                 juce::Rectangle<int> bounds,
                                                 float intensity)
{
    if (bounds.isEmpty()) return;

    const float w  = (float) bounds.getWidth();
    const float h  = (float) bounds.getHeight();
    const float cx = (float) bounds.getCentreX();
    const float top = (float) bounds.getY() + 2.0f;

    const juce::Colour stroke = NeomodernLookAndFeel::ACCENT_CYAN
        .withAlpha(0.45f + 0.55f * intensity);
    const juce::Colour suctionFill = NeomodernLookAndFeel::ACCENT_CYAN
        .withAlpha(0.55f + 0.45f * intensity);

    // ── Curva sinuosa (corpo del tentacolo) ──
    juce::Path tentacle;
    tentacle.startNewSubPath(cx, top);
    tentacle.cubicTo(cx + w * 0.6f, top + h * 0.20f,
                     cx - w * 0.55f, top + h * 0.55f,
                     cx + w * 0.30f, top + h * 0.92f);

    g.setColour(stroke);
    g.strokePath(tentacle, juce::PathStrokeType(2.6f + intensity * 1.4f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // ── Ventose (cerchietti decrescenti lungo la curva) ──
    const float pathLen = tentacle.getLength();
    const float ts[] = { 0.22f, 0.45f, 0.68f, 0.88f };
    const float rs[] = { 3.8f, 3.2f, 2.6f, 2.0f };

    for (int i = 0; i < 4; ++i)
    {
        auto pt = tentacle.getPointAlongPath(ts[i] * pathLen);
        const float r = rs[i] * (0.85f + 0.15f * intensity);

        g.setColour(suctionFill);
        g.fillEllipse(pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);

        g.setColour(NeomodernLookAndFeel::PANEL_LIGHT);
        const float ri = r * 0.5f;
        g.fillEllipse(pt.x - ri, pt.y - ri, ri * 2.0f, ri * 2.0f);
    }
}

void StereoCompressorEditor::paint(juce::Graphics& g)
{
    // ── Background: pannello chiaro brushed con gradiente verticale ──
    {
        juce::ColourGradient bgGrad(NeomodernLookAndFeel::PANEL_LIGHT_HI,
                                     0.0f, 0.0f,
                                     NeomodernLookAndFeel::PANEL_LIGHT.darker(0.04f),
                                     0.0f, (float) getHeight(),
                                     false);
        g.setGradientFill(bgGrad);
        g.fillRect(getLocalBounds());
    }

    // ── Header ──
    {
        g.setColour(NeomodernLookAndFeel::TEXT_DARK);
        g.setFont(juce::Font("Helvetica Neue", 19.0f, juce::Font::bold));
        g.drawText("STEREO  COMPRESSOR",
                   0, 10, getWidth(), 26,
                   juce::Justification::centred);

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN_DIM.withAlpha(0.6f));
        g.setFont(juce::Font("Helvetica Neue", 9.0f, juce::Font::plain));
        g.drawText("v1.1  ·  UTILITY PACK 01",
                   0, 36, getWidth(), 12,
                   juce::Justification::centred);

        g.setColour(NeomodernLookAndFeel::KNOB_RIM.withAlpha(0.4f));
        g.drawLine(40.0f, 54.0f, (float) getWidth() - 40.0f, 54.0f, 1.0f);
    }

    // ── Tentacolo (modulato da habisso) ──
    {
        const float hab = processor.apvts.getRawParameterValue("habisso")->load() * 0.01f;
        lastHabissoVisual = hab;
        paintTentacleIcon(g, tentacleArea, hab);
    }
}

void StereoCompressorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    area.removeFromTop(50); // header

    // ── Meter sinistro / destro ──
    auto leftMeterArea  = area.removeFromLeft(42);
    inMeter.setBounds(leftMeterArea);
    area.removeFromLeft(10);

    auto rightMeterArea = area.removeFromRight(42);
    outMeter.setBounds(rightMeterArea);
    area.removeFromRight(10);

    // ── Display freq response ──
    auto displayArea = area.removeFromTop(160);
    freqDisplay.setBounds(displayArea);
    area.removeFromTop(10);

    // ── Riga 1: HP / LP (sx)  |  THR / ATT / REL / MAK (dx) ──
    auto row1 = area.removeFromTop(135);

    auto filterArea = row1.removeFromLeft(180);
    {
        const int kw = 80;
        auto a1 = filterArea.removeFromLeft(kw);
        hpFreq.label .setBounds(a1.removeFromTop(16));
        hpFreq.slider.setBounds(a1);

        filterArea.removeFromLeft(10);

        auto a2 = filterArea.removeFromLeft(kw);
        lpFreq.label .setBounds(a2.removeFromTop(16));
        lpFreq.slider.setBounds(a2);
    }

    row1.removeFromLeft(20);

    KnobGroup* compGroups[] = { &threshold, &attack, &release, &makeup };
    const int compKnobW = row1.getWidth() / 4;
    for (auto* kg : compGroups)
    {
        auto a = row1.removeFromLeft(compKnobW);
        kg->label .setBounds(a.removeFromTop(16));
        kg->slider.setBounds(a);
    }

    area.removeFromTop(6);

    // ── Riga 2: RATIO (sx) | HABISSO + tentacle | WIDTH ──
    auto row2 = area.removeFromTop(110);

    auto ratioArea = row2.removeFromLeft(230);
    ratioLabel.setBounds(ratioArea.removeFromTop(18));
    ratioArea.removeFromTop(4);
    auto buttonsArea = ratioArea.removeFromTop(46);
    {
        const int bw = buttonsArea.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            ratioButtons[i].setBounds(buttonsArea.removeFromLeft(bw).reduced(3));
    }

    row2.removeFromLeft(30);

    // HABISSO: knob + tentacle a destra del knob
    auto habArea = row2.removeFromLeft(140);
    habisso.label.setBounds(habArea.removeFromTop(16));
    auto habKnobArea = habArea.removeFromLeft(95);
    habisso.slider.setBounds(habKnobArea);
    tentacleArea = habArea.reduced(2, 4); // resto = area tentacolo

    row2.removeFromLeft(30);

    auto widthArea = row2.removeFromLeft(95);
    width.label .setBounds(widthArea.removeFromTop(16));
    width.slider.setBounds(widthArea);
}

juce::AudioProcessorEditor* StereoCompressorProcessor::createEditor()
{
    return new StereoCompressorEditor(*this);
}
