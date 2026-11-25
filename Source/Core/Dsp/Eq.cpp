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

enum Index { lowCut0, lowCut1, lowShelf0, lowShelf1, midPeak, highShelf0, highShelf1, highCut0, highCut1 };

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
} // namespace

Eq::Chain Eq::design (const EqSettings& s, double fs)
{
    Chain c;
    cut (c, lowCut0, s.lowCutOn, s.lowCutSlope, s.lowCutFreq, fs, false);
    shelf (c, lowShelf0, s.lowGain, s.lowSlope, s.lowFreq, fs, false);
    c[midPeak] = s.midGain == 0.0f ? BiquadCoeffs::identity() : design::peak (s.midFreq, s.midGain, s.midWidth, fs);
    shelf (c, highShelf0, s.highGain, s.highSlope, s.highFreq, fs, true);
    cut (c, highCut0, s.highCutOn, s.highCutSlope, s.highCutFreq, fs, true);
    return c;
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
    c.v = { std::log (static_cast<double> (s.lowCutFreq)), static_cast<double> (s.lowGain), std::log (static_cast<double> (s.lowFreq)),
            static_cast<double> (s.midGain), std::log (static_cast<double> (s.midFreq)), std::log (static_cast<double> (s.midWidth)),
            static_cast<double> (s.highGain), std::log (static_cast<double> (s.highFreq)), std::log (static_cast<double> (s.highCutFreq)) };
    return c;
}

void Eq::Continuous::applyTo (EqSettings& s) const
{
    s.lowCutFreq = static_cast<float> (std::exp (v[0]));
    s.lowGain = static_cast<float> (v[1]);
    s.lowFreq = static_cast<float> (std::exp (v[2]));
    s.midGain = static_cast<float> (v[3]);
    s.midFreq = static_cast<float> (std::exp (v[4]));
    s.midWidth = static_cast<float> (std::exp (v[5]));
    s.highGain = static_cast<float> (v[6]);
    s.highFreq = static_cast<float> (std::exp (v[7]));
    s.highCutFreq = static_cast<float> (std::exp (v[8]));
}

Eq::Discrete Eq::Discrete::from (const EqSettings& s)
{
    Discrete d;
    d.lowCutOn = s.lowCutOn;
    d.highCutOn = s.highCutOn;
    d.lowCutSlope = s.lowCutSlope;
    d.lowSlope = s.lowSlope;
    d.highSlope = s.highSlope;
    d.highCutSlope = s.highCutSlope;
    return d;
}

void Eq::Discrete::applyTo (EqSettings& s) const
{
    s.lowCutOn = lowCutOn;
    s.highCutOn = highCutOn;
    s.lowCutSlope = lowCutSlope;
    s.lowSlope = lowSlope;
    s.highSlope = highSlope;
    s.highCutSlope = highCutSlope;
}

bool Eq::Discrete::operator== (const Discrete& o) const
{
    return lowCutOn == o.lowCutOn && highCutOn == o.highCutOn && lowCutSlope == o.lowCutSlope && lowSlope == o.lowSlope
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
    for (size_t k = 0; k < sections.size(); ++k)
        sections[k].setCoeffs (c[k]);
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

void Eq::redesign()
{
    for (int k = 0; k < 2; ++k)
    {
        // フェードが終わっている間は、使っている方だけ作る
        if (k != active && fade >= 1.0)
            continue;
        auto& p = paths[static_cast<size_t> (k)];
        EqSettings s;
        current.applyTo (s);
        p.discrete.applyTo (s);
        p.setCoeffs (design (s, fs));
    }
}

bool Eq::stepContinuous()
{
    bool moved = false;
    for (size_t i = 0; i < current.v.size(); ++i)
    {
        const double d = target.v[i] - current.v[i];
        if (d == 0.0)
            continue;
        current.v[i] = std::abs (d) < kSnap ? target.v[i] : current.v[i] + d * smoothCoef;
        moved = true;
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
        redesign();
    }
}

void Eq::process (float* data, int n)
{
    for (int i = 0; i < n; ++i)
    {
        if (--untilUpdate < 0)
        {
            untilUpdate = kUpdateInterval - 1;
            bool changed = stepContinuous();
            // 段の数が変わる切り替えは、前の切り替えのフェードが終わってから始める（続けて動かされても、2 つの間だけでつなぐ）
            if (fade >= 1.0 && targetDiscrete != paths[static_cast<size_t> (active)].discrete)
            {
                const int next = 1 - active;
                paths[static_cast<size_t> (next)] = paths[static_cast<size_t> (active)]; // 状態ごと写してから係数を替える
                paths[static_cast<size_t> (next)].discrete = targetDiscrete;
                active = next;
                fade = 0.0;
                changed = true;
            }
            if (changed)
                redesign();
        }

        const double x = data[i];
        double y = paths[static_cast<size_t> (active)].process (x);
        if (fade < 1.0)
        {
            const double old = paths[static_cast<size_t> (1 - active)].process (x);
            fade = std::min (1.0, fade + fadeStep);
            y = old + (y - old) * fade;
        }
        data[i] = static_cast<float> (y);
    }
}

} // namespace vcab
