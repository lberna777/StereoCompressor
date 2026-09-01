#include "PluginEditor.h"

// ═════════════════════════════════════════════════════════════════════════════
//   FreqResponseDisplay
// ═════════════════════════════════════════════════════════════════════════════

FreqResponseDisplay::FreqResponseDisplay(StereoCompressorProcessor& p)
    : processor(p)
{
    std::fill(spectrumDB.begin(), spectrumDB.end(), -90.0f);
    startTimerHz(30);
}

FreqResponseDisplay::~FreqResponseDisplay() { stopTimer(); }

void FreqResponseDisplay::timerCallback()
{
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

        for (int b = 0; b < kBins; ++b)
        {
            const float t    = (float) b / (float) (kBins - 1);
            const float freq = 20.0f * std::pow(1000.0f, t);
            float binF = freq / (float)(sr * 0.5) * (float)(N / 2);
            binF = juce::jlimit(0.0f, (float)(N / 2 - 1), binF);
            const int bi = (int) binF;

            const float mag = fftWork[(size_t) bi] / (float)(N / 2);
            const float dB  = juce::Decibels::gainToDecibels(mag, -90.0f);

            if (dB > spectrumDB[(size_t) b]) spectrumDB[(size_t) b] = dB;
            else spectrumDB[(size_t) b] = spectrumDB[(size_t) b] * 0.82f + dB * 0.18f;
        }
    }
    else
    {
        for (auto& v : spectrumDB)
            v = v * 0.95f - 90.0f * 0.05f;
    }

    repaint();
}

void FreqResponseDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background sonar
    {
        juce::ColourGradient bgGrad(juce::Colour(0xff060e18),
                                     bounds.getCentreX(), bounds.getY(),
                                     juce::Colour(0xff08121e),
                                     bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    }

    auto plotArea  = bounds.reduced(8.0f);
    auto grBarArea = plotArea.removeFromBottom(8.0f);
    plotArea.removeFromBottom(4.0f);

    const float xMin = plotArea.getX();
    const float xW   = plotArea.getWidth();
    const float yTop = plotArea.getY();
    const float yBot = plotArea.getBottom();
    const float yH   = plotArea.getHeight();

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

    // Grid teal fosforescente
    {
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.08f));
        for (int f : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
            g.drawVerticalLine((int) freqToX((float) f), yTop, yBot);
        for (int dB : { 0, -12, -24, -36 })
            g.drawHorizontalLine((int) dBToY((float) dB), xMin, plotArea.getRight());

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.20f));
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

    // Spettro input (mountain teal fosforescente)
    {
        juce::Path mountain;
        mountain.startNewSubPath(xMin, yBot);
        for (int b = 0; b < kBins; ++b)
        {
            float t    = (float) b / (float) (kBins - 1);
            float freq = 20.0f * std::pow(1000.0f, t);
            float x    = freqToX(freq);
            float dB   = juce::jlimit(DB_BOT, DB_TOP, spectrumDB[(size_t) b] + 6.0f);
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

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.70f));
        g.strokePath(mountain, juce::PathStrokeType(1.0f));
    }

    // Curva filtro HP × LP con Q (phosphor green)
    {
        const double sr  = processor.getCurrentSampleRate();
        const float hpF  = juce::jlimit(20.0f,   500.0f,  processor.apvts.getRawParameterValue("hpFreq")->load());
        const float lpF  = juce::jlimit(2000.0f, 20000.0f,processor.apvts.getRawParameterValue("lpFreq")->load());
        const float hpQ  = juce::jlimit(0.1f,    6.0f,    processor.apvts.getRawParameterValue("hpQ"   )->load());
        const float lpQ  = juce::jlimit(0.1f,    6.0f,    processor.apvts.getRawParameterValue("lpQ"   )->load());

        auto hpCoef = StereoCompressorProcessor::makeHighPassWithQ(sr, hpF, hpQ);
        auto lpCoef = StereoCompressorProcessor::makeLowPassWithQ (sr, lpF, lpQ);

        juce::Path curve;
        const int N = 240;
        for (int i = 0; i <= N; ++i)
        {
            float t    = (float) i / (float) N;
            float freq = 20.0f * std::pow(1000.0f, t);
            double mag = hpCoef->getMagnitudeForFrequency((double) freq, sr)
                       * lpCoef->getMagnitudeForFrequency((double) freq, sr);
            float dB   = (float) juce::Decibels::gainToDecibels(mag);
            float x    = freqToX(freq);
            float y    = dBToY(juce::jlimit(DB_BOT, DB_TOP, dB));
            if (i == 0) curve.startNewSubPath(x, y);
            else        curve.lineTo(x, y);
        }

        // Glow teal
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.22f));
        g.strokePath(curve, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        // Linea principale fosforescente
        g.setColour(juce::Colour(0xff00ffcc).withAlpha(0.90f));
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        auto drawMarker = [&](float freq, juce::Colour c)
        {
            float x = freqToX(juce::jlimit(20.0f, 20000.0f, freq));
            float y = dBToY(-3.0f);
            g.setColour(c.withAlpha(0.95f));
            g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colour(0xff00ffcc).withAlpha(0.6f));
            g.drawEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.0f);
        };
        if (hpF > 22.0f)    drawMarker(hpF, NeomodernLookAndFeel::ACCENT_CYAN);
        if (lpF < 19000.0f) drawMarker(lpF, NeomodernLookAndFeel::ACCENT_CYAN);
    }

    // GR bar
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(grBarArea, 2.0f);

        float grAbs  = juce::jlimit(0.0f, 20.0f, -displayedGR);
        float grNorm = grAbs / 20.0f;
        auto  fillR  = grBarArea.withWidth(grBarArea.getWidth() * grNorm);
        g.setColour(NeomodernLookAndFeel::METER_RED.withAlpha(0.85f));
        g.fillRoundedRectangle(fillR, 2.0f);

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText("GR  " + juce::String(displayedGR, 1) + " dB",
                   grBarArea.toNearestInt(), juce::Justification::centredRight);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//   VerticalMeter
// ═════════════════════════════════════════════════════════════════════════════

VerticalMeter::VerticalMeter(StereoCompressorProcessor& p, Side s)
    : processor(p), side(s)
{
    startTimerHz(30);
}

VerticalMeter::~VerticalMeter() { stopTimer(); }

void VerticalMeter::timerCallback()
{
    const float tL = (side == Input) ? processor.getInputLevelDB(0)  : processor.getOutputLevelDB(0);
    const float tR = (side == Input) ? processor.getInputLevelDB(1)  : processor.getOutputLevelDB(1);

    auto smooth = [](float& cur, float target)
    {
        if (target > cur) cur = target;
        else              cur = cur * 0.86f + target * 0.14f;
    };
    smooth(displayedL, tL);
    smooth(displayedR, tR);

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

    {
        juce::ColourGradient bgGrad(NeomodernLookAndFeel::PANEL_DARK.darker(0.1f),
                                     bounds.getCentreX(), bounds.getY(),
                                     NeomodernLookAndFeel::PANEL_DARK,
                                     bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    }

    // Bioluminescent glow aura
    {
        float avgLevel = ((displayedL + 60.0f) + (displayedR + 60.0f)) / 120.0f;
        float glowLevel = juce::jlimit(0.0f, 1.0f, avgLevel);
        juce::ColourGradient glow(
            NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(glowLevel * 0.20f),
            bounds.getCentreX(), bounds.getCentreY(),
            NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.0f),
            bounds.getCentreX(), bounds.getBottom(), true);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(bounds.expanded(4.0f), 6.0f);
    }

    auto inner = bounds.reduced(4.0f);
    auto labelArea = inner.removeFromBottom(14.0f);
    g.setColour(NeomodernLookAndFeel::TEXT_MUTED);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(side == Input ? "IN" : "OUT", labelArea.toNearestInt(), juce::Justification::centred);
    inner.removeFromBottom(2.0f);

    const float minDb = -60.0f, maxDb = 6.0f;
    auto dBToNorm = [&](float dB)
    {
        return juce::jmap(juce::jlimit(minDb, maxDb, dB), minDb, maxDb, 0.0f, 1.0f);
    };

    const float gap  = 2.0f;
    const float barW = (inner.getWidth() - gap) * 0.5f;
    auto barL = juce::Rectangle<float>(inner.getX(),              inner.getY(), barW, inner.getHeight());
    auto barR = juce::Rectangle<float>(inner.getX() + barW + gap, inner.getY(), barW, inner.getHeight());

    auto drawBar = [&](juce::Rectangle<float> r, float dB, float hold)
    {
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

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    for (int dB : { -40, -20, -12, -6, -3, 0 })
    {
        const float y = inner.getBottom() - inner.getHeight() * dBToNorm((float) dB);
        g.drawHorizontalLine((int) y, inner.getX(), inner.getRight());
    }

    g.setColour(NeomodernLookAndFeel::METER_RED.withAlpha(0.5f));
    const float y0 = inner.getBottom() - inner.getHeight() * dBToNorm(0.0f);
    g.drawHorizontalLine((int) y0, inner.getX(), inner.getRight());
}

// ═════════════════════════════════════════════════════════════════════════════
//   OctopusDisplay
// ═════════════════════════════════════════════════════════════════════════════

OctopusDisplay::OctopusDisplay(StereoCompressorProcessor& p) : processor(p) {}

void OctopusDisplay::update(float eqAggr, float widthNorm, float comprNorm, float habNorm)
{
    const float a = 0.12f;
    smoothEqAggr    = smoothEqAggr    * (1.0f - a) + eqAggr    * a;
    smoothWidthNorm = smoothWidthNorm * (1.0f - a) + widthNorm * a;
    smoothCompr     = smoothCompr     * (1.0f - a) + comprNorm * a;
    smoothHab       = smoothHab       * (1.0f - a) + habNorm   * a;

    wavePhase += 0.08f;
    if (wavePhase >= juce::MathConstants<float>::twoPi)
        wavePhase -= juce::MathConstants<float>::twoPi;
}

void OctopusDisplay::paintBackground(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad(juce::Colour(0xff060810), b.getCentreX(), b.getY(),
                               juce::Colour(0xff0a2030), b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRect(b);
}

void OctopusDisplay::paintWaterSurface(juce::Graphics& g, float waterY)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();

    juce::Path wave;
    wave.startNewSubPath(0.0f, waterY);
    for (int xi = 0; xi <= (int)W; xi += 2)
    {
        float xf = (float)xi;
        float yw = waterY
                 + std::sin(xf * 0.035f + wavePhase)        * 2.8f
                 + std::sin(xf * 0.018f + wavePhase * 0.7f) * 1.8f;
        wave.lineTo(xf, yw);
    }
    wave.lineTo(W, H);
    wave.lineTo(0.0f, H);
    wave.closeSubPath();

    juce::ColourGradient waterGrad(juce::Colour(0xff1a4a5a).withAlpha(0.55f),
                                    W * 0.5f, waterY,
                                    juce::Colour(0xff1a4a5a).withAlpha(0.18f),
                                    W * 0.5f, H, false);
    g.setGradientFill(waterGrad);
    g.fillPath(wave);

    // Solo la linea della superficie
    juce::Path waveLine;
    waveLine.startNewSubPath(0.0f, waterY);
    for (int xi = 0; xi <= (int)W; xi += 2)
    {
        float xf = (float)xi;
        float yw = waterY
                 + std::sin(xf * 0.035f + wavePhase)        * 2.8f
                 + std::sin(xf * 0.018f + wavePhase * 0.7f) * 1.8f;
        waveLine.lineTo(xf, yw);
    }
    g.setColour(NeomodernLookAndFeel::WATER_SURFACE.withAlpha(0.9f));
    g.strokePath(waveLine, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

void OctopusDisplay::paintOctopusHead(juce::Graphics& g, float cx, float headY, float compr)
{
    const float headW = 110.0f + compr * 20.0f;
    const float headH = 88.0f  + compr * 16.0f;

    // Aura esterna
    g.setColour(juce::Colour(0xff2a1540).withAlpha(0.35f));
    g.fillEllipse(cx - headW * 0.5f - 22, headY - headH * 0.5f - 22,
                  headW + 44, headH + 44);

    // Corpo
    juce::ColourGradient bodyGrad(juce::Colour(0xff4a2860),
                                   cx, headY - headH * 0.3f,
                                   juce::Colour(0xff2a1540),
                                   cx, headY + headH * 0.5f, false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(cx - headW * 0.5f, headY - headH * 0.5f, headW, headH);

    // Highlight speculare
    g.setColour(juce::Colours::white.withAlpha(0.10f + compr * 0.08f));
    g.fillEllipse(cx - headW * 0.22f - headW * 0.14f,
                  headY - headH * 0.38f,
                  headW * 0.28f, headH * 0.22f);

    // Occhi
    const float eyeBaseR = 9.0f + compr * 7.0f;
    const float eyeOffX  = headW * 0.22f;
    const float eyeOffY  = headH * 0.12f;

    for (int side = -1; side <= 1; side += 2)
    {
        const float ecx = cx + side * eyeOffX;
        const float ecy = headY - eyeOffY;
        const float r   = eyeBaseR;

        // Outer glow
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.22f + compr * 0.35f));
        g.fillEllipse(ecx - r * 2.0f, ecy - r * 2.0f, r * 4.0f, r * 4.0f);

        // Iris
        g.setColour(juce::Colour(0xff0a1020));
        g.fillEllipse(ecx - r, ecy - r, r * 2.0f, r * 2.0f);

        // Pupilla
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.55f + compr * 0.45f));
        g.fillEllipse(ecx - r * 0.5f, ecy - r * 0.5f, r, r);

        // Sopracciglio arrabbiato
        juce::Line<float> brow;
        if (side < 0)
            brow = { ecx - r * 1.1f, ecy - r * 1.3f, ecx + r * 0.8f, ecy - r * 1.8f };
        else
            brow = { ecx - r * 0.8f, ecy - r * 1.8f, ecx + r * 1.1f, ecy - r * 1.3f };

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.7f));
        g.drawLine(brow, 2.0f);
    }

    // Bocca (cipiglio verso il basso)
    {
        const float mouthY = headY + headH * 0.28f;
        const float mouthW = headW * 0.38f;
        juce::Path mouth;
        mouth.startNewSubPath(cx - mouthW * 0.5f, mouthY - 4.0f);
        mouth.quadraticTo(cx, mouthY + 8.0f + compr * 6.0f,
                          cx + mouthW * 0.5f, mouthY - 4.0f);
        g.setColour(juce::Colour(0xff0a1020).withAlpha(0.85f));
        g.strokePath(mouth, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Bargigli (3 corte curve sotto la mascella, controllati da habiss)
    {
        const float bStartY = headY + headH * 0.42f;
        const float offsets[] = { -24.0f, 0.0f, 24.0f };
        for (int bi = 0; bi < 3; ++bi)
        {
            float bx = cx + offsets[bi];
            float curl = (bi == 1) ? 0.0f : offsets[bi] * 0.25f;
            juce::Path barg;
            barg.startNewSubPath(bx, bStartY);
            barg.cubicTo(bx + curl, bStartY + 8.0f,
                         bx - curl, bStartY + 18.0f,
                         bx + curl * 0.5f, bStartY + 28.0f + smoothHab * 10.0f);
            g.setColour(juce::Colour(0xff4a2860).withAlpha(0.7f));
            g.strokePath(barg, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
    }
}

void OctopusDisplay::paintSuctionCups(juce::Graphics& g, const juce::Path& path, float aggr)
{
    const float pathLen = path.getLength();
    const float ts[] = { 0.18f, 0.36f, 0.52f, 0.68f, 0.82f };
    const float rs[] = { 4.5f,  3.8f,  3.0f,  2.4f,  1.8f  };

    for (int i = 0; i < 5; ++i)
    {
        auto pt = path.getPointAlongPath(ts[i] * pathLen);
        const float r = rs[i] * (0.8f + 0.2f * aggr);

        g.setColour(juce::Colour(0xff0a1020).withAlpha(0.75f));
        g.fillEllipse(pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);

        const float ri = r * 0.48f;
        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.35f + aggr * 0.45f));
        g.fillEllipse(pt.x - ri, pt.y - ri, ri * 2.0f, ri * 2.0f);
    }
}

void OctopusDisplay::paintTentacle(juce::Graphics& g, int index, float cx, float baseY,
                                     float aggr, float /*waterY*/)
{
    const bool isLeft = (index < 4);
    float spreadFactor, originX;

    if (isLeft)
    {
        spreadFactor = (float)(4 - index) / 4.0f;   // 1.0, 0.75, 0.5, 0.25
        originX = cx - 10.0f - (float)(3 - index) * 18.0f;
    }
    else
    {
        spreadFactor = (float)(index - 3) / 4.0f;   // 0.25, 0.50, 0.75, 1.0
        originX = cx + 10.0f + (float)(index - 4) * 18.0f;
    }

    const float L = 160.0f + aggr * 90.0f;
    float endX, endY, cp1x, cp1y, cp2x, cp2y;

    if (isLeft)
    {
        const float calmEndX  = originX - 40.0f * spreadFactor;
        const float calmEndY  = baseY + L * 0.85f;
        const float calmCP1x  = originX - 20.0f * spreadFactor;
        const float calmCP1y  = baseY + L * 0.30f;
        const float calmCP2x  = originX - 35.0f * spreadFactor;
        const float calmCP2y  = baseY + L * 0.60f;

        const float aggrEndX  = originX - (150.0f + 100.0f * spreadFactor);
        const float aggrEndY  = baseY + L * 0.70f;
        const float aggrCP1x  = originX + 30.0f * spreadFactor;
        const float aggrCP1y  = baseY + L * 0.20f;
        const float aggrCP2x  = originX - (180.0f + 60.0f * spreadFactor);
        const float aggrCP2y  = baseY + L * 0.50f;

        endX  = calmEndX  + aggr * (aggrEndX  - calmEndX);
        endY  = calmEndY  + aggr * (aggrEndY  - calmEndY);
        cp1x  = calmCP1x  + aggr * (aggrCP1x  - calmCP1x);
        cp1y  = calmCP1y  + aggr * (aggrCP1y  - calmCP1y);
        cp2x  = calmCP2x  + aggr * (aggrCP2x  - calmCP2x);
        cp2y  = calmCP2y  + aggr * (aggrCP2y  - calmCP2y);
    }
    else
    {
        const float monoEndX  = originX + 10.0f * spreadFactor;
        const float monoEndY  = baseY + L * 0.75f;
        const float monoCP1x  = originX + 5.0f  * spreadFactor;
        const float monoCP1y  = baseY + L * 0.28f;
        const float monoCP2x  = originX + 8.0f  * spreadFactor;
        const float monoCP2y  = baseY + L * 0.55f;

        const float wideEndX  = originX + (150.0f + 100.0f * spreadFactor);
        const float wideEndY  = baseY + L * 0.72f;
        const float wideCP1x  = originX - 25.0f * spreadFactor;
        const float wideCP1y  = baseY + L * 0.18f;
        const float wideCP2x  = originX + (180.0f + 60.0f * spreadFactor);
        const float wideCP2y  = baseY + L * 0.52f;

        endX  = monoEndX  + aggr * (wideEndX  - monoEndX);
        endY  = monoEndY  + aggr * (wideEndY  - monoEndY);
        cp1x  = monoCP1x  + aggr * (wideCP1x  - monoCP1x);
        cp1y  = monoCP1y  + aggr * (wideCP1y  - monoCP1y);
        cp2x  = monoCP2x  + aggr * (wideCP2x  - monoCP2x);
        cp2y  = monoCP2y  + aggr * (wideCP2y  - monoCP2y);
    }

    juce::Path path;
    path.startNewSubPath(originX, baseY);
    path.cubicTo(cp1x, cp1y, cp2x, cp2y, endX, endY);

    // Doppio stroke per effetto rastremamento
    g.setColour(juce::Colour(0xff2a1540).withAlpha(0.92f));
    g.strokePath(path, juce::PathStrokeType(6.5f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xff4a2860).withAlpha(0.75f));
    g.strokePath(path, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    paintSuctionCups(g, path, aggr);
}

void OctopusDisplay::paint(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float cx = W * 0.5f;
    const float waterY = H * 0.577f;

    const float headRestY = waterY + 10.0f;
    const float headPeakY = H * 0.23f;
    const float headY = headRestY - smoothCompr * (headRestY - headPeakY);
    const float headH = 88.0f + smoothCompr * 16.0f;
    const float baseY = headY + headH * 0.48f;

    paintBackground(g);

    // Tentacoli (disegnati PRIMA dell'acqua → sembrare sott'acqua)
    for (int i = 0; i < 4; ++i)
        paintTentacle(g, i,     cx, baseY, smoothEqAggr,    waterY);
    for (int i = 4; i < 8; ++i)
        paintTentacle(g, i,     cx, baseY, smoothWidthNorm, waterY);

    paintWaterSurface(g, waterY);
    paintOctopusHead(g, cx, headY, smoothCompr);
}

// ═════════════════════════════════════════════════════════════════════════════
//   StereoCompressorEditor
// ═════════════════════════════════════════════════════════════════════════════

StereoCompressorEditor::StereoCompressorEditor(StereoCompressorProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      freqDisplay(p),
      inMeter(p, VerticalMeter::Input),
      outMeter(p, VerticalMeter::Output),
      octopusDisplay(p)
{
    setLookAndFeel(&lnf);
    setSize(880, 580);

    // OctopusDisplay deve essere il primo layer (sfondo)
    addAndMakeVisible(octopusDisplay);

    setupKnob(hpFreq,     "hpFreq",     "HI-PASS");
    setupKnob(hpQ,        "hpQ",        "HP Q");
    setupKnob(lpFreq,     "lpFreq",     "LO-PASS");
    setupKnob(lpQ,        "lpQ",        "LP Q");
    setupKnob(threshold,  "threshold",  "THRESHOLD");
    setupKnob(ratio,      "ratio",      "RATIO");
    setupKnob(attack,     "attack",     "ATTACK");
    setupKnob(release,    "release",    "RELEASE");
    setupKnob(makeup,     "makeup",     "MAKEUP");
    setupKnob(habiss,     "habiss",     "HABISS");
    setupKnob(paralarva,  "paralarva",  "PARALARVA");

    addAndMakeVisible(freqDisplay);
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);

    startTimerHz(30);
}

StereoCompressorEditor::~StereoCompressorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void StereoCompressorEditor::setupKnob(KnobGroup& grp,
                                        const juce::String& paramID,
                                        const juce::String& name)
{
    grp.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    grp.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    grp.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible(grp.slider);

    grp.label.setText(name, juce::dontSendNotification);
    grp.label.setFont(juce::Font(10.0f, juce::Font::bold));
    grp.label.setColour(juce::Label::textColourId, NeomodernLookAndFeel::TEXT_MUTED);
    grp.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(grp.label);

    grp.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, grp.slider);
}

void StereoCompressorEditor::timerCallback()
{
    updateOctopusParameters();
}

void StereoCompressorEditor::updateOctopusParameters()
{
    // EQ aggressiveness → tentacoli sinistra (0-3)
    const float hpF = processor.apvts.getRawParameterValue("hpFreq")->load();
    const float lpF = processor.apvts.getRawParameterValue("lpFreq")->load();
    const float hpAggr = (hpF - 20.0f) / 480.0f;
    const float lpAggr = (20000.0f - lpF) / 18000.0f;
    const float eqAggr = juce::jlimit(0.0f, 1.0f, std::max(hpAggr, lpAggr));

    // Paralarva (stereo width) → tentacoli destra (4-7)
    const float parVal   = processor.apvts.getRawParameterValue("paralarva")->load();
    const float widthNorm = juce::jlimit(0.0f, 1.0f, parVal / 2.0f);

    // Compressione → emergenza testa
    const float grNorm      = juce::jlimit(0.0f, 1.0f, -processor.getGainReductionDB() / 20.0f);
    const float thrVal      = processor.apvts.getRawParameterValue("threshold")->load();
    const float ratioVal    = processor.apvts.getRawParameterValue("ratio")->load();
    const float thrNorm     = juce::jlimit(0.0f, 1.0f, (thrVal + 60.0f) / 60.0f);
    const float ratioNorm   = juce::jlimit(0.0f, 1.0f, (ratioVal - 1.0f) / 19.0f);
    const float staticPot   = thrNorm * ratioNorm * 0.40f;
    const float comprNorm   = std::max(grNorm, staticPot);

    // Habiss → bargigli del mento
    const float habNorm = processor.apvts.getRawParameterValue("habiss")->load() / 100.0f;

    octopusDisplay.update(eqAggr, widthNorm, comprNorm, habNorm);
    octopusDisplay.repaint();
}

void StereoCompressorEditor::paintParalarvaIcon(juce::Graphics& g,
                                                  juce::Rectangle<int> bounds,
                                                  float parVal)
{
    if (bounds.isEmpty()) return;

    // parVal è il valore normalizzato 0..1 (paralarva/2.0)
    const float cx  = (float)bounds.getCentreX();
    const float bot = (float)bounds.getBottom() - 4.0f;
    const float maxH = (float)bounds.getHeight() - 8.0f;

    // 5 tentacoli: idx 0-4, centro a idx 2
    // offset X e altezza massima per ciascuno
    const float offsets[] = { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f };

    // Altezze dei tentacoli in funzione di parVal
    // - Centrale (2): sempre visibile, cresce con parVal
    // - Inner pair (1,3): cresce con parVal
    // - Outer pair (0,4): appare solo sopra 0.3, cresce di più
    const float minH = 10.0f;

    auto tentacleHeight = [&](int idx) -> float
    {
        float dist = std::abs(offsets[idx]) / 24.0f;  // 0 = centro, 1 = esterno
        float minFraction = 0.25f * (1.0f - dist * 0.7f);
        float t = minFraction + parVal * (1.0f - minFraction);
        return minH + t * (maxH - minH);
    };

    for (int i = 0; i < 5; ++i)
    {
        float tx  = cx + offsets[i];
        float th  = tentacleHeight(i);
        float top = bot - th;

        // Intensità visiva: tentacoli esterni più luminosi a alta width
        float dist = std::abs(offsets[i]) / 24.0f;
        float alpha = 0.55f + parVal * 0.45f - dist * (1.0f - parVal) * 0.4f;
        alpha = juce::jlimit(0.15f, 1.0f, alpha);

        juce::Path tent;
        float curl = offsets[i] * 0.3f;
        tent.startNewSubPath(tx, bot);
        tent.cubicTo(tx + curl * 0.4f, bot - th * 0.3f,
                     tx - curl * 0.3f, bot - th * 0.65f,
                     tx + curl * 0.15f, top);

        g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(alpha));
        g.strokePath(tent, juce::PathStrokeType(2.4f + parVal * 1.2f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // 2 ventose per tentacolo
        const float pLen = tent.getLength();
        for (float t : { 0.35f, 0.70f })
        {
            auto pt = tent.getPointAlongPath(t * pLen);
            float r = 2.2f + parVal * 1.0f;
            g.setColour(juce::Colour(0xff0a1020).withAlpha(0.7f));
            g.fillEllipse(pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
            float ri = r * 0.45f;
            g.setColour(NeomodernLookAndFeel::ACCENT_CYAN.withAlpha(0.35f + parVal * 0.45f));
            g.fillEllipse(pt.x - ri, pt.y - ri, ri * 2.0f, ri * 2.0f);
        }
    }
}

void StereoCompressorEditor::paint(juce::Graphics& g)
{
    // Sfondo per le strisce laterali dei meter (non coperte da OctopusDisplay)
    g.fillAll(NeomodernLookAndFeel::OCEAN_DEEP);

    // Header
    g.setColour(NeomodernLookAndFeel::TEXT_DARK);
    g.setFont(juce::Font("Helvetica Neue", 22.0f, juce::Font::bold));
    g.drawText("NEMO",
               0, 10, getWidth(), 28,
               juce::Justification::centred);

    g.setColour(NeomodernLookAndFeel::ACCENT_CYAN_DIM.withAlpha(0.7f));
    g.setFont(juce::Font("Helvetica Neue", 9.0f, juce::Font::plain));
    g.drawText("stereo-compressor",
               0, 38, getWidth(), 12,
               juce::Justification::centred);

    g.setColour(NeomodernLookAndFeel::WATER_SURFACE.withAlpha(0.35f));
    g.drawLine(40.0f, 54.0f, (float)getWidth() - 40.0f, 54.0f, 1.0f);

    // Grafica tentacoli Paralarva
    {
        const float parVal = juce::jlimit(0.0f, 1.0f,
            processor.apvts.getRawParameterValue("paralarva")->load() / 2.0f);
        paintParalarvaIcon(g, paralarvaIconArea, parVal);
    }
}

void StereoCompressorEditor::resized()
{
    // OctopusDisplay copre tutto lo schermo come layer di sfondo
    octopusDisplay.setBounds(getLocalBounds());

    auto area = getLocalBounds().reduced(12);
    area.removeFromTop(54); // header

    // ── Meter sinistro / destro ──
    auto leftMeterArea  = area.removeFromLeft(44);
    inMeter.setBounds(leftMeterArea);
    area.removeFromLeft(10);

    auto rightMeterArea = area.removeFromRight(44);
    outMeter.setBounds(rightMeterArea);
    area.removeFromRight(10);

    // ── FreqResponseDisplay ──
    auto displayArea = area.removeFromTop(140);
    freqDisplay.setBounds(displayArea);
    area.removeFromTop(8);

    // ── Riga 1: HP Freq | HP Q  |  LP Freq | LP Q ──
    auto row1 = area.removeFromTop(120);
    {
        const int kw = row1.getWidth() / 4;
        KnobGroup* filterKnobs[] = { &hpFreq, &hpQ, &lpFreq, &lpQ };
        for (auto* kg : filterKnobs)
        {
            auto a = row1.removeFromLeft(kw);
            kg->label .setBounds(a.removeFromTop(16));
            kg->slider.setBounds(a);
        }
    }
    area.removeFromTop(8);

    // ── Riga 2: Threshold | Ratio | Attack | Release | Makeup ──
    auto row2 = area.removeFromTop(120);
    {
        const int kw = row2.getWidth() / 5;
        KnobGroup* compKnobs[] = { &threshold, &ratio, &attack, &release, &makeup };
        for (auto* kg : compKnobs)
        {
            auto a = row2.removeFromLeft(kw);
            kg->label .setBounds(a.removeFromTop(16));
            kg->slider.setBounds(a);
        }
    }
    area.removeFromTop(8);

    // ── Riga 3: Habiss + tentacolo | Paralarva + tentacoli ──
    auto row3 = area;

    // Habiss: knob (90px) + tentacle icon (resto fino a 220px)
    auto habArea = row3.removeFromLeft(220);
    habiss.label.setBounds(habArea.removeFromTop(16));
    auto habKnobA = habArea.removeFromLeft(90);
    habiss.slider.setBounds(habKnobA);
    // Tentacolo habiss (il vecchio paintTentacleIcon) — disegnato dall'OctopusDisplay

    row3.removeFromLeft(40);

    // Paralarva: knob + tentacle graphic
    auto parArea = row3.removeFromLeft(280);
    paralarva.label.setBounds(parArea.removeFromTop(16));
    auto parKnobA = parArea.removeFromLeft(90);
    paralarva.slider.setBounds(parKnobA);
    parArea.removeFromLeft(8);
    paralarvaIconArea = parArea.reduced(2, 4);  // area per la grafica tentacoli
}

juce::AudioProcessorEditor* StereoCompressorProcessor::createEditor()
{
    return new StereoCompressorEditor(*this);
}
