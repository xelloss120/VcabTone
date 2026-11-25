// EQ（3 章）: ローカット、低域（下のシェルフ）、中域（ピーク）、高域（上のシェルフ）、ハイカット。
// 双二次 9 段（各帯域 2 段まで。中域は 1 段）。使わない段は素通しの係数にする。
#pragma once

#include "../Params.h"
#include "Biquad.h"

#include <array>

namespace vcab
{

class Eq
{
public:
    static constexpr int kSections = 9;
    using Chain = std::array<BiquadCoeffs, kSections>;

    // 設定から全段の係数を作る（音の処理と画面の曲線で同じものを使う）
    static Chain design (const EqSettings& s, double fs);
    // 周波数 f での増減（dB）
    static double responseDb (const Chain& chain, double f, double fs);

    void prepare (double fs);
    // ブロックごとに呼ぶ。連続の値（周波数、増減、幅）はなめらかに追い、
    // 段の数が変わる切り替え（傾き、オン/オフ）は、新旧の係数の出力をクロスフェードする
    void setTarget (const EqSettings& s);
    void process (float* data, int n);

private:
    // 連続の値（周波数と幅は対数、増減は dB）
    struct Continuous
    {
        std::array<double, 9> v {};
        static Continuous from (const EqSettings& s);
        void applyTo (EqSettings& s) const;
    };
    // 段の数を変える値
    struct Discrete
    {
        bool lowCutOn = true, highCutOn = true;
        Slope lowCutSlope = Slope::db24, lowSlope = Slope::db24, highSlope = Slope::db24, highCutSlope = Slope::db24;
        static Discrete from (const EqSettings& s);
        void applyTo (EqSettings& s) const;
        bool operator== (const Discrete& o) const;
        bool operator!= (const Discrete& o) const { return ! (*this == o); }
    };
    struct Path
    {
        Discrete discrete;
        std::array<Biquad, kSections> sections;
        double process (double x);
        void setCoeffs (const Chain& c);
    };

    double fs = 48000.0;
    Continuous current, target;
    Discrete targetDiscrete;
    std::array<Path, 2> paths;
    int active = 0;
    double fade = 1.0;     // 1 なら active だけ。0〜1 の間は、もう片方（古い方）から active へ移っている途中
    double fadeStep = 0.0;
    double smoothCoef = 0.0;
    int untilUpdate = 0;
    bool fresh = true;     // prepare の直後（最初の設定は、追わずにそのまま使う）

    void redesign();
    bool stepContinuous(); // 連続の値を 1 歩追う。動いたら true
};

} // namespace vcab
