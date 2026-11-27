#include "DeEsser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vcab
{

namespace
{
constexpr int kShelfUpdateInterval = 8;    // 削るシェルフを作り直す間隔（標本）
constexpr double kShelfSnapDb = 0.005;     // 削り量がこれだけ変わったら作り直す
constexpr double kFreqSmoothSeconds = 0.02; // 境目を動かしたときに、削るシェルフの位置を追う時定数

double toDb (double power) { return 10.0 * std::log10 (std::max (power, 1.0e-20)); }
} // namespace

void DeEsser::prepare (double sampleRate)
{
    fs = sampleRate;
    frameLength = std::max (1, static_cast<int> (std::lround (tuning.frameSeconds * fs)));
    inFrame = 0;
    upperSum = lowerSum = 0.0;
    const auto historyLength = static_cast<size_t> (std::max (1L, std::lround (tuning.gateWindowSeconds / tuning.frameSeconds)));
    upperHistory.assign (historyLength, 0.0);
    lowerHistory.assign (historyLength, 0.0);
    historyPos = historyFilled = 0;
    framesSinceSilent = historyLength;
    noiseValid = false;
    averageValid = false;
    average = 0.0;
    targetDb = cutDb = 0.0;
    sibilant = false;
    frames = 0;
    attackCoef = 1.0 - std::exp (-1.0 / (tuning.attackSeconds * fs));
    releaseCoef = 1.0 - std::exp (-1.0 / (tuning.releaseSeconds * fs));
    averageCoef = std::exp (-(frameLength / fs) / tuning.averageSeconds);
    freqCoef = 1.0 - std::exp (-kShelfUpdateInterval / (kFreqSmoothSeconds * fs));
    for (auto& f : upperFilters)
        f.reset();
    for (auto& f : lowerFilters)
        f.reset();
    shelf.reset();
    shelf.setCoeffs (BiquadCoeffs::identity());
    designedSplit = designedLow = designedHigh = -1.0f;
    shelfDesignedFreq = shelfWarpedFreq = -1.0;
    shelfDesignedDb = 0.0;
    shelfFreq = shelfTargetFreq = settings.split;
    untilShelfUpdate = 0;
    designDetectors();
}

void DeEsser::setSettings (const DeessSettings& s)
{
    settings = s;
    shelfTargetFreq = s.split;
    if (s.split != designedSplit || s.low != designedLow || s.high != designedHigh)
        designDetectors();
}

void DeEsser::designDetectors()
{
    // 判定の帯域は音に出ないので、つまみを動かしたらすぐ作り直す（状態は残す）
    const double split = settings.split, low = settings.low, high = settings.high;
    for (int k = 0; k < 2; ++k)
    {
        upperFilters[static_cast<size_t> (k)].setCoeffs (design::highPass2 (split, design::kButterworth4Q[k], fs));
        lowerFilters[static_cast<size_t> (k)].setCoeffs (design::highPass2 (low, design::kButterworth4Q[k], fs));
        lowerFilters[static_cast<size_t> (k + 2)].setCoeffs (design::lowPass2 (split, design::kButterworth4Q[k], fs));
    }
    // 上端がナイキストの近く（以上）なら、ローパスはかけない
    upperFilters[2].setCoeffs (high < design::kMaxFreqRatio * fs ? design::lowPass2 (high, design::kButterworth2Q, fs) : BiquadCoeffs::identity());
    designedSplit = settings.split;
    designedLow = settings.low;
    designedHigh = settings.high;
}

void DeEsser::endFrame()
{
    const double upper = upperSum / frameLength, lower = lowerSum / frameLength;
    upperSum = lowerSum = 0.0;
    ++frames;

    const double upperDb = toDb (upper), lowerDb = toDb (lower);
    const double ratioNow = upperDb - lowerDb;

    // 雑音と声の判定に使う値: 直近の区切りの平均（窓でならす）。1ms の値は雑音でも ±10dB ほど揺れ、
    // 雑音の値が揺れの底に張り付いて、黙っている間の雑音を声と数えてしまうため（4.4）
    upperHistory[historyPos] = upper;
    lowerHistory[historyPos] = lower;
    historyPos = (historyPos + 1) % upperHistory.size();
    historyFilled = std::min (historyFilled + 1, upperHistory.size());
    double upperGate = 0.0, lowerGate = 0.0;
    for (size_t k = 0; k < upperHistory.size(); ++k)
    {
        upperGate += upperHistory[k];
        lowerGate += lowerHistory[k];
    }
    // 起動直後は、埋まった区切りだけで平均する（0 で埋めた分で小さく出て、雑音を低く見ないように）
    upperGate /= static_cast<double> (historyFilled);
    lowerGate /= static_cast<double> (historyFilled);
    const double upperGateDb = toDb (upperGate), lowerGateDb = toDb (lowerGate);

    // 4.4 黙っている間の雑音: 下がるときはすぐ、上がるときはゆっくり。
    // 次のときは、雑音の値を変えない。デジタルの無音（0）やミュートで雑音の値が底まで落ちると、戻るまで数十秒かかるため。
    //   - ならした値が、声の下限より雑音との差だけ小さい所（-92dBFS）より下（そこより下の雑音は、声の判定に関わらない）
    //   - 窓の中に、無音とみなす区切り（1ms の値が -120dBFS より下）がある（無音との境目で、ならした値が下がるため）
    framesSinceSilent = toDb (lower) < tuning.silenceDb ? 0 : framesSinceSilent + 1;
    if (lowerGateDb >= tuning.voiceFloorDb - tuning.voiceOverNoiseDb && framesSinceSilent >= upperHistory.size())
    {
        if (! noiseValid || lowerGateDb < noiseDb)
            noiseDb = lowerGateDb;
        else
            noiseDb = std::min (lowerGateDb, noiseDb + tuning.noiseRiseDbPerSecond * frameLength / fs);
        noiseValid = true;
    }

    // 4.3 声が出ていた間の平均（声が出ている区切りだけで更新する）
    const bool voiced = noiseValid && lowerGateDb >= noiseDb + tuning.voiceOverNoiseDb && lowerGateDb >= tuning.voiceFloorDb
                        && upperGateDb - lowerGateDb <= tuning.vowelRatioDb;
    if (voiced)
    {
        if (! averageValid)
        {
            average = lowerGate;
            averageValid = true;
        }
        else
        {
            average = averageCoef * average + (1.0 - averageCoef) * lowerGate;
        }
    }

    // 4.5 削る判定と削る量
    const double averageDb = averageValid ? toDb (average) : std::numeric_limits<double>::quiet_NaN();
    const bool sibilantLike = averageValid && ratioNow > tuning.sibilantRatioDb;
    double target = 0.0;
    if (sibilantLike && settings.on)
    {
        const double over = std::max (0.0, (upperDb - averageDb) - settings.sensitivity);
        target = std::min (static_cast<double> (settings.maxCut), over * tuning.slope);
    }
    targetDb = target;
    // 画面の「検出」の印: サ行らしく、上の帯域が黙っている間の雑音より十分大きい（無音の雑音で灯らないように）。
    // ディエッサーがオフなら灯さない（ほかのつまみと同じく、働いていない表示にそろえる。2026-11-27）
    sibilant = settings.on && sibilantLike && upperDb >= noiseDb + tuning.voiceOverNoiseDb;

    if (trace != nullptr)
        trace->push_back ({ static_cast<double> (frames * frameLength) / fs, static_cast<float> (upperDb), static_cast<float> (lowerDb),
                            static_cast<float> (averageDb), static_cast<float> (noiseDb), static_cast<float> (upperGateDb),
                            static_cast<float> (lowerGateDb), static_cast<float> (target),
                            static_cast<uint8_t> (voiced ? 1 : 0), static_cast<uint8_t> (sibilantLike ? 1 : 0) });
}

void DeEsser::process (float* data, int n)
{
    blockPeakCut = 0.0;
    blockSibilantSeen = false;
    for (int i = 0; i < n; ++i)
    {
        const double x = data[i];

        // 判定（区切りの終わりで、目標の削り量が決まる）
        double u = x, l = x;
        for (auto& f : upperFilters)
            u = f.process (u);
        for (auto& f : lowerFilters)
            l = f.process (l);
        upperSum += u * u;
        lowerSum += l * l;
        if (++inFrame >= frameLength)
        {
            inFrame = 0;
            endFrame();
            blockSibilantSeen = blockSibilantSeen || sibilant;
        }

        // 削り量をなめらかに追う（dB のまま）
        cutDb += (targetDb - cutDb) * (targetDb > cutDb ? attackCoef : releaseCoef);
        if (cutDb < 1.0e-4 && targetDb == 0.0)
            cutDb = 0.0;
        blockPeakCut = std::max (blockPeakCut, cutDb);

        // 削るシェルフを作り直す（削っていない間は素通し）
        if (--untilShelfUpdate < 0)
        {
            untilShelfUpdate = kShelfUpdateInterval - 1;
            shelfFreq += (shelfTargetFreq - shelfFreq) * freqCoef;
            if (std::abs (shelfTargetFreq - shelfFreq) < 0.1)
                shelfFreq = shelfTargetFreq;
            if (std::abs (cutDb - shelfDesignedDb) >= kShelfSnapDb || (cutDb == 0.0 && shelfDesignedDb != 0.0)
                || (cutDb > 0.0 && shelfFreq != shelfDesignedFreq))
            {
                if (cutDb == 0.0)
                    shelf.setCoeffs (BiquadCoeffs::identity());
                else
                {
                    // 位置が前と同じなら、周波数の写し（tan）は前の値を使う（削っている間は、増減だけが 8 標本ごとに変わるため）
                    if (shelfFreq != shelfWarpedFreq)
                    {
                        shelfWarped = design::warpedFreq (shelfFreq, fs);
                        shelfWarpedFreq = shelfFreq;
                    }
                    shelf.setCoeffs (design::highShelf2Warped (shelfWarped, -cutDb, tuning.shelfQ));
                }
                shelfDesignedDb = cutDb;
                shelfDesignedFreq = shelfFreq;
            }
        }
        data[i] = static_cast<float> (shelf.process (x));
    }
}

} // namespace vcab
