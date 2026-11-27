#include "Biquad.h"
#include "MathConstants.h"

#include <algorithm>
#include <cmath>

namespace vcab
{

std::complex<double> BiquadCoeffs::response (double f, double fs) const
{
    const double w = 2.0 * kPi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

namespace design
{

namespace
{

// 周波数をそろえた双一次変換の K = tan(π f / fs)
double warp (double f, double fs)
{
    const double r = std::clamp (f / fs, 1.0e-6, kMaxFreqRatio);
    return std::tan (kPi * r);
}

// アナログの 1 次 (B0 s + B1) / (A0 s + A1)（s は周波数 1 にそろえたもの）を写す
BiquadCoeffs bilinear1 (double B0, double B1, double A0, double A1, double K)
{
    const double n0 = B0 / K + B1, n1 = B1 - B0 / K;
    const double d0 = A0 / K + A1, d1 = A1 - A0 / K;
    BiquadCoeffs c;
    c.b0 = n0 / d0;
    c.b1 = n1 / d0;
    c.b2 = 0.0;
    c.a1 = d1 / d0;
    c.a2 = 0.0;
    return c;
}

// アナログの 2 次 (B0 s² + B1 s + B2) / (A0 s² + A1 s + A2) を写す
BiquadCoeffs bilinear2 (double B0, double B1, double B2, double A0, double A1, double A2, double K)
{
    const double K2 = K * K;
    const double n0 = B0 + B1 * K + B2 * K2, n1 = 2.0 * (B2 * K2 - B0), n2 = B0 - B1 * K + B2 * K2;
    const double d0 = A0 + A1 * K + A2 * K2, d1 = 2.0 * (A2 * K2 - A0), d2 = A0 - A1 * K + A2 * K2;
    BiquadCoeffs c;
    c.b0 = n0 / d0;
    c.b1 = n1 / d0;
    c.b2 = n2 / d0;
    c.a1 = d1 / d0;
    c.a2 = d2 / d0;
    return c;
}

} // namespace

BiquadCoeffs lowPass1 (double f, double fs) { return bilinear1 (0.0, 1.0, 1.0, 1.0, warp (f, fs)); }
BiquadCoeffs highPass1 (double f, double fs) { return bilinear1 (1.0, 0.0, 1.0, 1.0, warp (f, fs)); }
BiquadCoeffs lowPass2 (double f, double q, double fs) { return bilinear2 (0.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0, warp (f, fs)); }
BiquadCoeffs highPass2 (double f, double q, double fs) { return bilinear2 (1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0, warp (f, fs)); }

// 1 次のシェルフ: H(s) = G (s/a + 1) / (s/b + 1)、a = √G、b = 1/√G（f で √G、つまり dB で半分）
BiquadCoeffs lowShelf1 (double f, double gainDb, double fs)
{
    const double g = std::pow (10.0, gainDb / 20.0), r = std::sqrt (g);
    return bilinear1 (r, g, r, 1.0, warp (f, fs));
}

BiquadCoeffs highShelf1 (double f, double gainDb, double fs)
{
    const double g = std::pow (10.0, gainDb / 20.0), r = std::sqrt (g);
    return bilinear1 (r, 1.0, 1.0 / r, 1.0, warp (f, fs));
}

// 2 次のシェルフ（RBJ）: A = 10^(dB/40)
BiquadCoeffs lowShelf2 (double f, double gainDb, double q, double fs)
{
    const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
    return bilinear2 (A, A * sA / q, A * A, A, sA / q, 1.0, warp (f, fs));
}

BiquadCoeffs highShelf2 (double f, double gainDb, double q, double fs)
{
    return highShelf2Warped (warp (f, fs), gainDb, q);
}

double warpedFreq (double f, double fs) { return warp (f, fs); }

BiquadCoeffs highShelf2Warped (double K, double gainDb, double q)
{
    const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
    return bilinear2 (A * A, A * sA / q, A, 1.0, sA / q, A, K);
}

// ピーク（RBJ）。幅は双一次変換で縮む分を補う（RBJ の BW の式と同じ）
BiquadCoeffs peak (double f, double gainDb, double octaves, double fs)
{
    const double ff = std::clamp (f, 1.0, kMaxFreqRatio * fs);
    const double w0 = 2.0 * kPi * ff / fs;
    const double q = 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * octaves * w0 / std::sin (w0)));
    const double A = std::pow (10.0, gainDb / 40.0);
    return bilinear2 (1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0, warp (ff, fs));
}

} // namespace design

} // namespace vcab
