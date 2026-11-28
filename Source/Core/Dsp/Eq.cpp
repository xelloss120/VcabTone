#include "Eq.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

namespace
{
constexpr int kUpdateInterval = 16;          // 連続の値を追って係数を作り直す間隔（標本）
constexpr double kSmoothSeconds = 0.02;      // 連続の値を追う時定数
constexpr double kFadeSeconds = 0.02;        // 段の数が変わるときのクロスフェード
constexpr double kSnap = 1.0e-4;             // ここまで近づいたら、狙いの値にそろえる

enum Index { lowCut0, lowCut1, low0, low1, mid0, mid1, high0, high1, highCut0, highCut1 };

void cut (Eq::Chain& c, int first, bool on, Slope slope, double f, double fs, bool high)
{
    c[static_cast<size_t> (first)] = BiquadCoeffs::identity();
    c[static_cast<size_t> (first + 1)] = BiquadCoeffs::identity();
    if (! on)
        return;
    switch (slope)
    {
        case Slope::db6:
            c[static_cast<size_t> (first)] = high ? design::lowPass1 (f, fs) : design::highPass1 (f, fs);
            break;
        case Slope::db12:
            c[static_cast<size_t> (first)] = high ? design::lowPass2 (f, design::kButterworth2Q, fs) : design::highPass2 (f, design::kButterworth2Q, fs);
            break;
        case Slope::db24:
            for (int k = 0; k < 2; ++k)
                c[static_cast<size_t> (first + k)] = high ? design::lowPass2 (f, design::kButterworth4Q[k], fs)
                                                          : design::highPass2 (f, design::kButterworth4Q[k], fs);
            break;
    }
}

void shelf (Eq::Chain& c, int first, double gainDb, Slope slope, double f, double fs, bool high)
{
    c[static_cast<size_t> (first)] = BiquadCoeffs::identity();
    c[static_cast<size_t> (first + 1)] = BiquadCoeffs::identity();
    if (gainDb == 0.0)
        return;
    switch (slope)
    {
        case Slope::db6:
            c[static_cast<size_t> (first)] = high ? design::highShelf1 (f, gainDb, fs) : design::lowShelf1 (f, gainDb, fs);
            break;
        case Slope::db12:
            c[static_cast<size_t> (first)] = high ? design::highShelf2 (f, gainDb, design::kButterworth2Q, fs)
                                                  : design::lowShelf2 (f, gainDb, design::kButterworth2Q, fs);
            break;
        case Slope::db24:
            // 4 次のバターワース形のシェルフ（Holters・Zölzer 2006）: 同じ周波数の 2 次 2 段に、増減を半分ずつ
            for (int k = 0; k < 2; ++k)
                c[static_cast<size_t> (first + k)] = high ? design::highShelf2 (f, gainDb * 0.5, design::kButterworth4Q[k], fs)
                                                          : design::lowShelf2 (f, gainDb * 0.5, design::kButterworth4Q[k], fs);
            break;
    }
}
// 低域・中域・高域の 1 本（2 段）。ベルは 1 段目だけ使う
void band (Eq::Chain& c, int first, Shape shape, double gainDb, double f, Slope slope, double octaves, double fs)
{
    if (shape == Shape::bell)
    {
        c[static_cast<size_t> (first)] = gainDb == 0.0 ? BiquadCoeffs::identity() : design::peak (f, gainDb, octaves, fs);
        c[static_cast<size_t> (first + 1)] = BiquadCoeffs::identity();
    }
    else
        shelf (c, first, gainDb, slope, f, fs, shape == Shape::highShelf);
}
} // namespace

Eq::Chain Eq::design (const EqSettings& s, double fs)
{
    Chain c;
    designBands (c, allBands, s, fs);
    return c;
}

void Eq::designBands (Chain& c, unsigned bands, const EqSettings& s, double fs)
{
    if (bands & lowCutBand)
        cut (c, lowCut0, s.lowCutOn, s.lowCutSlope, s.lowCutFreq, fs, false);
    if (bands & lowBand)
        band (c, low0, s.lowShape, s.lowGain, s.lowFreq, s.lowSlope, s.lowWidth, fs);
    if (bands & midBand)
        band (c, mid0, s.midShape, s.midGain, s.midFreq, s.midSlope, s.midWidth, fs);
    if (bands & highBand)
        band (c, high0, s.highShape, s.highGain, s.highFreq, s.highSlope, s.highWidth, fs);
    if (bands & highCutBand)
        cut (c, highCut0, s.highCutOn, s.highCutSlope, s.highCutFreq, fs, true);
}

double Eq::responseDb (const Chain& chain, double f, double fs)
{
    double mag2 = 1.0;
    for (const auto& c : chain)
        if (! c.isIdentity())
            mag2 *= std::norm (c.response (f, fs));
    return 10.0 * std::log10 (std::max (mag2, 1.0e-30));
}

Eq::Continuous Eq::Continuous::from (const EqSettings& s)
{
    Continuous c;
    c.v = { std::log (static_cast<double> (s.lowCutFreq)),
            static_cast<double> (s.lowGain), std::log (static_cast<double> (s.lowFreq)), std::log (static_cast<double> (s.lowWidth)),
            static_cast<double> (s.midGain), std::log (static_cast<double> (s.midFreq)), std::log (static_cast<double> (s.midWidth)),
            static_cast<double> (s.highGain), std::log (static_cast<double> (s.highFreq)), std::log (static_cast<double> (s.highWidth)),
            std::log (static_cast<double> (s.highCutFreq)) };
    return c;
}

void Eq::Continuous::applyTo (EqSettings& s) const
{
    s.lowCutFreq = static_cast<float> (std::exp (v[0]));
    s.lowGain = static_cast<float> (v[1]);
    s.lowFreq = static_cast<float> (std::exp (v[2]));
    s.lowWidth = static_cast<float> (std::exp (v[3]));
    s.midGain = static_cast<float> (v[4]);
    s.midFreq = static_cast<float> (std::exp (v[5]));
    s.midWidth = static_cast<float> (std::exp (v[6]));
    s.highGain = static_cast<float> (v[7]);
    s.highFreq = static_cast<float> (std::exp (v[8]));
    s.highWidth = static_cast<float> (std::exp (v[9]));
    s.highCutFreq = static_cast<float> (std::exp (v[10]));
}

Eq::Discrete Eq::Discrete::from (const EqSettings& s)
{
    Discrete d;
    d.lowCutOn = s.lowCutOn;
    d.highCutOn = s.highCutOn;
    d.lowShape = s.lowShape;
    d.midShape = s.midShape;
    d.highShape = s.highShape;
    d.lowCutSlope = s.lowCutSlope;
    d.lowSlope = s.lowSlope;
    d.midSlope = s.midSlope;
    d.highSlope = s.highSlope;
    d.highCutSlope = s.highCutSlope;
    return d;
}

void Eq::Discrete::applyTo (EqSettings& s) const
{
    s.lowCutOn = lowCutOn;
    s.highCutOn = highCutOn;
    s.lowShape = lowShape;
    s.midShape = midShape;
    s.highShape = highShape;
    s.lowCutSlope = lowCutSlope;
    s.lowSlope = lowSlope;
    s.midSlope = midSlope;
    s.highSlope = highSlope;
    s.highCutSlope = highCutSlope;
}

bool Eq::Discrete::operator== (const Discrete& o) const
{
    return lowCutOn == o.lowCutOn && highCutOn == o.highCutOn && lowShape == o.lowShape && midShape == o.midShape
           && highShape == o.highShape && lowCutSlope == o.lowCutSlope && lowSlope == o.lowSlope && midSlope == o.midSlope
           && highSlope == o.highSlope && highCutSlope == o.highCutSlope;
}

double Eq::Path::process (double x)
{
    for (auto& s : sections)
        x = s.process (x);
    return x;
}

void Eq::Path::setCoeffs (const Chain& c)
{
    coeffs = c;
    for (size_t k = 0; k < sections.size(); ++k)
        sections[k].setCoeffs (c[k]);
}

void Eq::Path::redesign (unsigned bands, const EqSettings& s, double fs)
{
    designBands (coeffs, bands, s, fs);
    for (size_t k = 0; k < sections.size(); ++k)
        sections[k].setCoeffs (coeffs[k]);
}

void Eq::prepare (double sampleRate)
{
    fs = sampleRate;
    smoothCoef = 1.0 - std::exp (-kUpdateInterval / (kSmoothSeconds * fs));
    fadeStep = 1.0 / (kFadeSeconds * fs);
    for (auto& p : paths)
        for (auto& s : p.sections)
            s.reset();
    active = 0;
    fade = 1.0;
    untilUpdate = 0;
    fresh = true;
}

void Eq::redesign (unsigned bands)
{
    if (bands == 0)
        return;
    EqSettings s;
    current.applyTo (s);
    for (int k = 0; k < 2; ++k)
    {
        // フェードが終わっている間は、使っている方だけ作る
        if (k != active && fade >= 1.0)
            continue;
        auto& p = paths[static_cast<size_t> (k)];
        p.discrete.applyTo (s);
        p.redesign (bands, s, fs);
    }
}

unsigned Eq::stepContinuous()
{
    // Continuous::v の並びの、それぞれの値が属する帯域
    static constexpr unsigned kBandOf[11] = { lowCutBand, lowBand, lowBand, lowBand, midBand, midBand, midBand,
                                              highBand, highBand, highBand, highCutBand };
    unsigned moved = 0;
    for (size_t i = 0; i < current.v.size(); ++i)
    {
        const double d = target.v[i] - current.v[i];
        if (d == 0.0)
            continue;
        current.v[i] = std::abs (d) < kSnap ? target.v[i] : current.v[i] + d * smoothCoef;
        moved |= kBandOf[i];
    }
    return moved;
}

void Eq::setTarget (const EqSettings& s)
{
    target = Continuous::from (s);
    targetDiscrete = Discrete::from (s);
    if (fresh)
    {
        fresh = false;
        current = target;
        paths[static_cast<size_t> (active)].discrete = targetDiscrete;
        fade = 1.0;
        redesign (allBands);
    }
}

void Eq::process (float* data, int n)
{
    for (int i = 0; i < n;)
    {
        if (--untilUpdate < 0)
        {
            untilUpdate = kUpdateInterval - 1;
            unsigned changed = stepContinuous();
            // 段の数が変わる切り替えは、前の切り替えのフェードが終わってから始める（続けて動かされても、2 つの間だけでつなぐ）
            if (fade >= 1.0 && targetDiscrete != paths[static_cast<size_t> (active)].discrete)
            {
                const int next = 1 - active;
                paths[static_cast<size_t> (next)] = paths[static_cast<size_t> (active)]; // 状態ごと写してから係数を替える
                paths[static_cast<size_t> (next)].discrete = targetDiscrete;
                active = next;
                fade = 0.0;
                changed = allBands;
            }
            redesign (changed);
        }

        // 次に係数を見直すまでの標本をまとめて処理する（untilUpdate は、この区間の残りの標本の数）
        const int len = std::min (n - i, untilUpdate + 1);
        untilUpdate -= len - 1;
        auto& cur = paths[static_cast<size_t> (active)];
        if (fade < 1.0)
        {
            auto& old = paths[static_cast<size_t> (1 - active)];
            for (int k = i; k < i + len; ++k)
            {
                const double x = data[k];
                double y = cur.process (x);
                if (fade < 1.0) // 区間の途中でフェードが終わったら、そこからは新しい方だけ
                {
                    const double o = old.process (x);
                    fade = std::min (1.0, fade + fadeStep);
                    y = o + (y - o) * fade;
                }
                data[k] = static_cast<float> (y);
            }
        }
        else
        {
            for (int k = i; k < i + len; ++k)
                data[k] = static_cast<float> (cur.process (data[k]));
        }
        i += len;
    }
}

} // namespace vcab
