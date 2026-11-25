#include "GraphView.h"
#include "EditorStyle.h"
#include "Dsp/Eq.h"

#include <cmath>

using namespace editor;

namespace
{
constexpr float kHitRadius = 11.0f;
constexpr float kPointRadius = 6.5f;
constexpr float kSmooth = 0.6f;            // スペクトルの、前のフレームの重み（パワー）
constexpr int kTimerHz = 30;
constexpr float kCutFallDbPerFrame = 0.5f; // 削り量の表示が下がる速さ（1 フレームあたり。約 15dB/秒）
constexpr double kSibilantHoldMs = 150.0;  // サ行の印を灯しておく時間
const char* const kPointNames[] = { "ローカット", "低域", "中域", "高域", "ハイカット" };
} // namespace

GraphView::GraphView (VcabToneProcessor& p) : proc (p)
{
    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / kFftSize);
    inRing.assign (kFftSize, 0.0f);
    outRing.assign (kFftSize, 0.0f);
    inPow.assign (kFftSize / 2 + 1, 0.0f);
    outPow.assign (kFftSize / 2 + 1, 0.0f);
    inDb.assign (kFftSize / 2 + 1, kMinDb - 20.0f);
    outDb.assign (kFftSize / 2 + 1, kMinDb - 20.0f);
    pullBuf.assign (SpectrumFifo::kSize, 0.0f);
    work.assign (2 * kFftSize, 0.0f);
    settings = proc.currentSettings();
    setTooltip (utf8 ("青は入力、橙は出力のスペクトル。黄の線は EQ の特性（右の目盛り）。\n"
                      "点をドラッグすると、EQ の周波数と増減を動かせます（ホイールで、中域は幅、低域・高域は増減。ダブルクリックで 0 に戻す）。\n"
                      "緑の帯と紫の帯は、ディエッサーが見ている下の帯域（判定の下端〜境目）と上の帯域（境目〜判定の上端）、赤の塗りはいま削っている量です。"));
    startTimerHz (kTimerHz);
}

GraphView::~GraphView()
{
    stopTimer();
}

bool GraphView::sibilantLit() const
{
    return juce::Time::getMillisecondCounterHiRes() < sibilantUntil;
}

void GraphView::timerCallback()
{
    // 入力と出力は、読める数が違うことがあるので、書く位置は別々に持つ（VcabShift と同じ）
    auto drain = [this] (SpectrumFifo& fifo, std::vector<float>& ring, int& pos)
    {
        const int n = fifo.pull (pullBuf.data(), static_cast<int> (pullBuf.size()));
        for (int i = 0; i < n; ++i)
            ring[static_cast<size_t> ((pos + i) % kFftSize)] = pullBuf[static_cast<size_t> (i)];
        pos = (pos + n) % kFftSize;
    };
    drain (proc.inputFifo, inRing, inRingPos);
    drain (proc.outputFifo, outRing, outRingPos);
    computeSpectrum (inRing, inRingPos, inPow, inDb);
    computeSpectrum (outRing, outRingPos, outPow, outDb);

    settings = proc.currentSettings();
    const float peak = proc.meters().cutPeakDb.exchange (0.0f);
    shownCut = std::max (peak, shownCut - kCutFallDbPerFrame);
    if (shownCut < 0.0f)
        shownCut = 0.0f;
    if (proc.meters().sibilantSeen.exchange (false))
        sibilantUntil = juce::Time::getMillisecondCounterHiRes() + kSibilantHoldMs;
    repaint();
}

void GraphView::computeSpectrum (const std::vector<float>& ring, int ringPos, std::vector<float>& power, std::vector<float>& db)
{
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        work[static_cast<size_t> (i)] = ring[static_cast<size_t> ((ringPos + i) % kFftSize)] * window[static_cast<size_t> (i)];
    fft.performRealOnlyForwardTransform (work.data(), true);
    // 窓の和で割って、正弦波の大きさが dBFS に近くなるようにする（表示のため）
    const float norm = 2.0f / (0.5f * kFftSize);
    for (int b = 0; b <= kFftSize / 2; ++b)
    {
        const float re = work[static_cast<size_t> (2 * b)] * norm, im = work[static_cast<size_t> (2 * b + 1)] * norm;
        auto& p = power[static_cast<size_t> (b)];
        p = kSmooth * p + (1.0f - kSmooth) * (re * re + im * im);
        db[static_cast<size_t> (b)] = juce::jlimit (kMinDb - 20.0f, kMaxDb + 20.0f, 10.0f * std::log10 (p + 1.0e-14f));
    }
}

juce::Rectangle<float> GraphView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (46.0f).withTrimmedRight (52.0f).withTrimmedTop (30.0f).withTrimmedBottom (26.0f);
}

float GraphView::maxHz() const
{
    const double fs = proc.sampleRate();
    return static_cast<float> (std::min (20000.0, fs > 0.0 ? fs * 0.5 : 24000.0));
}

float GraphView::xForHz (float hz) const
{
    const auto a = plotArea();
    return a.getX() + std::log (hz / kMinHz) / std::log (maxHz() / kMinHz) * a.getWidth();
}

float GraphView::hzForX (float x) const
{
    const auto a = plotArea();
    return kMinHz * std::pow (maxHz() / kMinHz, (x - a.getX()) / a.getWidth());
}

float GraphView::yForDb (float db) const
{
    const auto a = plotArea();
    return a.getY() + (kMaxDb - db) / (kMaxDb - kMinDb) * a.getHeight();
}

float GraphView::yForEq (float db) const
{
    const auto a = plotArea();
    return a.getCentreY() - db / kEqRange * a.getHeight() * 0.5f;
}

float GraphView::eqForY (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * kEqRange;
}

vcab::P GraphView::freqParam (int i)
{
    const vcab::P ids[] = { vcab::P::lowCutFreq, vcab::P::lowFreq, vcab::P::midFreq, vcab::P::highFreq, vcab::P::highCutFreq };
    return ids[i];
}

vcab::P GraphView::gainParam (int i)
{
    const vcab::P ids[] = { vcab::P::count, vcab::P::lowGain, vcab::P::midGain, vcab::P::highGain, vcab::P::count };
    return ids[i];
}

bool GraphView::pointOn (int i) const
{
    if (i == 0)
        return settings.eq.lowCutOn;
    if (i == 4)
        return settings.eq.highCutOn;
    return true;
}

juce::Point<float> GraphView::pointPos (int i) const
{
    // 点は曲線の上に置く: 中域は（周波数、増減）、シェルフは増減が半分になる所（周波数、増減 ÷ 2）、
    // カットは -3dB の所（オフなら 0dB の線）
    const auto a = plotArea();
    const float hz = proc.value (freqParam (i));
    float db = 0.0f;
    if (gainParam (i) == vcab::P::count)
        db = pointOn (i) ? -3.0f : 0.0f;
    else
        db = proc.value (gainParam (i)) * (isShelf (i) ? 0.5f : 1.0f);
    return { juce::jlimit (a.getX(), a.getRight(), xForHz (hz)), yForEq (db) };
}

int GraphView::hitPoint (juce::Point<float> p) const
{
    int best = -1;
    float bestD = kHitRadius;
    for (int i = 0; i < 5; ++i)
    {
        const float d = pointPos (i).getDistanceFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void GraphView::paint (juce::Graphics& g)
{
    g.fillAll (kGraph);
    const auto a = plotArea();
    g.setFont (juce::FontOptions (kFont - 3.0f));
    const double fs = proc.sampleRate() > 0.0 ? proc.sampleRate() : 48000.0;

    // 目盛り: 周波数
    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        if (f > maxHz())
            continue;
        const float x = xForHz (f);
        g.setColour (kGrid);
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.setColour (kDim);
        const auto label = f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String (f, 0);
        g.drawText (label, juce::Rectangle<float> (x - 30.0f, a.getBottom() + 3.0f, 60.0f, 18.0f), juce::Justification::centred);
    }
    // 目盛り: 左 dB（スペクトル）
    for (float db = kMinDb; db <= kMaxDb; db += 24.0f)
    {
        const float y = yForDb (db);
        g.setColour (kGrid);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (kDim);
        g.drawText (juce::String (db, 0), juce::Rectangle<float> (0.0f, y - 9.0f, a.getX() - 6.0f, 18.0f), juce::Justification::centredRight);
    }
    // 目盛り: 右 dB（EQ）
    for (float db = -kEqRange; db <= kEqRange; db += 3.0f)
    {
        const float y = yForEq (db);
        g.setColour (kEqColour.withAlpha (db == 0.0f ? 0.3f : 0.08f));
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (kEqColour.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db, 0), juce::Rectangle<float> (a.getRight() + 6.0f, y - 9.0f, 46.0f, 18.0f),
                    juce::Justification::centredLeft);
    }
    g.setColour (kDim);
    g.drawText ("dB", juce::Rectangle<float> (0.0f, 4.0f, a.getX() - 6.0f, 20.0f), juce::Justification::centredRight);
    g.setColour (kEqColour.withAlpha (0.8f));
    g.drawText ("EQ dB", juce::Rectangle<float> (a.getRight() - 4.0f, 4.0f, 56.0f, 20.0f), juce::Justification::centredLeft);

    // 凡例
    {
        float x = a.getX() + 4.0f;
        auto legend = [&] (juce::Colour c, const char* text)
        {
            g.setColour (c);
            g.fillRect (x, 13.0f, 16.0f, 3.0f);
            g.setColour (kText);
            const auto s = utf8 (text);
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s) + 4.0f;
            g.drawText (s, juce::Rectangle<float> (x + 20.0f, 4.0f, w, 20.0f), juce::Justification::centredLeft);
            x += 26.0f + w;
        };
        legend (kInputColour, "入力");
        legend (kOutputColour, "出力");
        legend (kEqColour, "EQ");
        legend (kLowerBandColour, "判定の下の帯域");
        legend (kSibilantColour, "判定の上の帯域");
        legend (kCutColour, "削り量");
    }

    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    // ディエッサーの判定の帯域（上の帯域と下の帯域を別の色で塗る。境目は実線、下端・上端は点線）
    const auto& d = settings.deess;
    {
        const float xl = xForHz (d.low), xs = xForHz (d.split), xh = xForHz (std::min (d.high, maxHz()));
        g.setColour (kLowerBandColour.withAlpha (d.on ? 0.07f : 0.03f));
        g.fillRect (juce::Rectangle<float> (xl, a.getY(), xs - xl, a.getHeight()));
        g.setColour (kSibilantColour.withAlpha (d.on ? 0.07f : 0.03f));
        g.fillRect (juce::Rectangle<float> (xs, a.getY(), xh - xs, a.getHeight()));
        const float dash[] = { 3.0f, 4.0f };
        for (auto [x, colour] : { std::pair<float, juce::Colour> { xl, kLowerBandColour }, { xh, kSibilantColour } })
        {
            juce::Path line, dashed;
            line.startNewSubPath (x, a.getY());
            line.lineTo (x, a.getBottom());
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dash, 2);
            g.setColour (colour.withAlpha (d.on ? 0.45f : 0.2f));
            g.fillPath (dashed);
        }
        g.setColour (kSibilantColour.withAlpha (d.on ? 0.6f : 0.25f));
        g.fillRect (juce::Rectangle<float> (xs - 0.5f, a.getY(), 1.0f, a.getHeight()));
    }

    // スペクトル
    auto spectrumPath = [&] (const std::vector<float>& db)
    {
        juce::Path path;
        bool started = false;
        for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
        {
            // 画素の幅の中の一番大きい区画（低い側は補間）
            const float f0 = hzForX (x), f1 = hzForX (x + 1.0f);
            const float b0 = static_cast<float> (f0 * kFftSize / fs), b1 = static_cast<float> (f1 * kFftSize / fs);
            float v;
            if (b1 - b0 < 1.0f)
            {
                const int i = juce::jlimit (0, kFftSize / 2 - 1, static_cast<int> (b0));
                const float t = b0 - static_cast<float> (i);
                v = db[static_cast<size_t> (i)] + t * (db[static_cast<size_t> (i + 1)] - db[static_cast<size_t> (i)]);
            }
            else
            {
                v = kMinDb - 20.0f;
                for (int i = static_cast<int> (b0); i <= std::min (static_cast<int> (b1), kFftSize / 2); ++i)
                    v = std::max (v, db[static_cast<size_t> (i)]);
            }
            const float y = yForDb (v);
            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
                path.lineTo (x, y);
        }
        return path;
    };
    g.setColour (kInputColour);
    g.strokePath (spectrumPath (inDb), juce::PathStrokeType (1.3f));
    g.setColour (kOutputColour);
    g.strokePath (spectrumPath (outDb), juce::PathStrokeType (1.3f));

    // ディエッサーの削り量（境目から上のシェルフ。0dB の線から下へ塗る）
    if (shownCut > 0.05f && d.on && ! settings.bypass)
    {
        const auto shelf = vcab::design::highShelf2 (d.split, -shownCut, vcab::DeEsser::Tuning {}.shelfQ, fs);
        juce::Path fill, line;
        const float y0 = yForEq (0.0f);
        fill.startNewSubPath (a.getX(), y0);
        bool started = false;
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const float db = static_cast<float> (20.0 * std::log10 (std::abs (shelf.response (hzForX (x), fs))));
            const float y = yForEq (juce::jmax (-kEqRange, db));
            fill.lineTo (x, y);
            if (! started)
            {
                line.startNewSubPath (x, y);
                started = true;
            }
            else
                line.lineTo (x, y);
        }
        fill.lineTo (a.getRight(), y0);
        fill.closeSubPath();
        g.setColour (kCutColour.withAlpha (0.22f));
        g.fillPath (fill);
        g.setColour (kCutColour);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    // EQ の特性
    {
        const auto chain = vcab::Eq::design (settings.eq, fs);
        juce::Path line;
        for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
        {
            const float db = static_cast<float> (vcab::Eq::responseDb (chain, hzForX (x), fs));
            const float y = yForEq (juce::jlimit (-kEqRange - 2.0f, kEqRange + 2.0f, db));
            if (x == a.getX())
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }
        g.setColour (kEqColour.withAlpha (settings.bypass ? 0.35f : 1.0f));
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }

    // EQ の点
    for (int i = 0; i < 5; ++i)
    {
        const auto p = pointPos (i);
        const float r = (i == hoverPoint || i == dragPoint) ? kPointRadius + 2.0f : kPointRadius;
        const auto box = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (p);
        if (pointOn (i))
        {
            g.setColour (kEqColour);
            g.fillEllipse (box);
        }
        else
        {
            g.setColour (kGraph);
            g.fillEllipse (box);
            g.setColour (kEqColour.withAlpha (0.7f));
            g.drawEllipse (box, 1.5f);
        }
        // 動かしている点と、マウスが乗っている点には、名前と値を添える
        if (i == hoverPoint || i == dragPoint)
        {
            const float hz = proc.value (freqParam (i));
            juce::String text = utf8 (kPointNames[i]) + "  " + (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz");
            if (gainParam (i) != vcab::P::count)
            {
                const float gdb = proc.value (gainParam (i));
                text << "  " << (gdb > 0.0f ? "+" : "") << juce::String (gdb, 1) << " dB";
            }
            if (i == 2)
                text << "  " << juce::String (proc.value (vcab::P::midWidth), 2) << " oct";
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f;
            auto tb = juce::Rectangle<float> (w, 20.0f).withCentre ({ p.x, p.y - 20.0f });
            if (tb.getY() < a.getY())
                tb.setY (p.y + 12.0f);
            tb = tb.constrainedWithin (a);
            g.setColour (kGraph.withAlpha (0.85f));
            g.fillRect (tb);
            g.setColour (kText);
            g.drawText (text, tb, juce::Justification::centred);
        }
    }

    g.restoreState();
    // サ行の検出と削り量の数字は、ディエッサーの帯のメーターに出す（グラフには出さない。2026-11-26）
}

void GraphView::setParam (vcab::P id, float v)
{
    auto* p = proc.param (id);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void GraphView::gesture (vcab::P id, bool start)
{
    if (id == vcab::P::count)
        return;
    if (start)
        proc.param (id)->beginChangeGesture();
    else
        proc.param (id)->endChangeGesture();
}

void GraphView::mouseMove (const juce::MouseEvent& e)
{
    const int before = hoverPoint;
    hoverPoint = hitPoint (e.position);
    setMouseCursor (hoverPoint < 0 ? juce::MouseCursor::NormalCursor
                    : gainParam (hoverPoint) == vcab::P::count ? juce::MouseCursor::LeftRightResizeCursor
                                                               : juce::MouseCursor::UpDownLeftRightResizeCursor);
    if (before != hoverPoint)
        repaint();
}

void GraphView::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint >= 0)
    {
        hoverPoint = -1;
        repaint();
    }
}

void GraphView::mouseDown (const juce::MouseEvent& e)
{
    dragPoint = hitPoint (e.position);
    if (dragPoint >= 0)
    {
        gesture (freqParam (dragPoint), true);
        gesture (gainParam (dragPoint), true);
    }
}

void GraphView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPoint < 0)
        return;
    const auto& fi = vcab::paramInfo (freqParam (dragPoint));
    setParam (freqParam (dragPoint), juce::jlimit (fi.minValue, fi.maxValue, hzForX (e.position.x)));
    if (gainParam (dragPoint) != vcab::P::count)
    {
        const auto& gi = vcab::paramInfo (gainParam (dragPoint));
        const float db = eqForY (e.position.y) * (isShelf (dragPoint) ? 2.0f : 1.0f);
        setParam (gainParam (dragPoint), juce::jlimit (gi.minValue, gi.maxValue, db));
    }
    settings = proc.currentSettings();
    repaint();
}

void GraphView::mouseUp (const juce::MouseEvent&)
{
    if (dragPoint >= 0)
    {
        gesture (freqParam (dragPoint), false);
        gesture (gainParam (dragPoint), false);
    }
    dragPoint = -1;
    repaint();
}

void GraphView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = hitPoint (e.position);
    if (i < 0)
        return;
    const auto id = gainParam (i) != vcab::P::count ? gainParam (i) : freqParam (i);
    gesture (id, true);
    setParam (id, gainParam (i) != vcab::P::count ? 0.0f : vcab::paramInfo (id).defaultValue);
    gesture (id, false);
    settings = proc.currentSettings();
    repaint();
}

void GraphView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f)
        return;
    const int i = hitPoint (e.position);
    if (i < 0 || gainParam (i) == vcab::P::count)
        return;
    const bool up = w.deltaY > 0.0f;
    const auto id = i == 2 ? vcab::P::midWidth : gainParam (i);
    const auto& info = vcab::paramInfo (id);
    // 幅は 1/12 オクターブずつ掛け算で、増減は 0.5dB ずつ
    const float v = i == 2 ? proc.value (id) * std::exp2 ((up ? 1.0f : -1.0f) / 12.0f) : proc.value (id) + (up ? 0.5f : -0.5f);
    gesture (id, true);
    setParam (id, juce::jlimit (info.minValue, info.maxValue, v));
    gesture (id, false);
    settings = proc.currentSettings();
    repaint();
}
