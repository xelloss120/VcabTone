// 双二次のフィルタ（直接形 I、倍精度）と、係数の設計。
// 係数は、アナログの原型（周波数を 1 にそろえたもの）を、周波数をそろえた双一次変換で写す（RBJ の Audio EQ Cookbook と同じ値になる）。
// 1 次のフィルタも、b2 = a2 = 0 の双二次として扱う。
#pragma once

#include <array>
#include <complex>

namespace vcab
{

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // a0 で割った後

    static BiquadCoeffs identity() { return {}; }
    bool isIdentity() const { return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0; }

    // 周波数 f（Hz）での複素数の応答
    std::complex<double> response (double f, double fs) const;
};

// 直接形 I。係数を途中で替えても、状態は入出力の値そのものなので、大きく乱れない（係数を動かすつまみに向く）
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& coeffs) { c = coeffs; }
    const BiquadCoeffs& coeffs() const { return c; }
    void reset() { x1 = x2 = y1 = y2 = 0.0; }

    double process (double x)
    {
        const double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

private:
    BiquadCoeffs c;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
};

namespace design
{

// ナイキストの近くでは双一次変換の写し方が極端になるので、周波数はこれより上にしない（標本化周波数に対する割合）
constexpr double kMaxFreqRatio = 0.45;

// 24dB/oct（4 次のバターワース）を 2 次 2 段に分けたときの Q
constexpr double kButterworth4Q[2] = { 0.54119610014619698, 1.3065629648763766 };
constexpr double kButterworth2Q = 0.70710678118654752;

BiquadCoeffs lowPass1 (double f, double fs);
BiquadCoeffs highPass1 (double f, double fs);
BiquadCoeffs lowPass2 (double f, double q, double fs);
BiquadCoeffs highPass2 (double f, double q, double fs);
// シェルフ。gainDb は低い側（lowShelf）か高い側（highShelf）の増減。f は増減が半分（dB で）になる所
BiquadCoeffs lowShelf1 (double f, double gainDb, double fs);
BiquadCoeffs highShelf1 (double f, double gainDb, double fs);
BiquadCoeffs lowShelf2 (double f, double gainDb, double q, double fs);
BiquadCoeffs highShelf2 (double f, double gainDb, double q, double fs);
// ピーク。幅はオクターブ（増減が半分（dB で）になる所の間）
BiquadCoeffs peak (double f, double gainDb, double octaves, double fs);

} // namespace design

} // namespace vcab
