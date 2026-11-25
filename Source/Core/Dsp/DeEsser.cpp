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
    noiseValid = false;
    averageValid = false;
    average = 0.0;
    targetDb = cutDb = 0.0;
    sibilant = false;
    frames = 0;
    attackCoef = 1.0 - std::exp (-1.0 / (tuning.attackSeconds * fs));
    releaseCoef = 1.0 - std::exp (-1.0 / (tuning.releaseSeconds * fs));
    for (auto& f : upperFilters)
        f.reset();
    for (auto& f : lowerFilters)
        f.reset();
    shelf.reset();
    shelf.setCoeffs (BiquadCoeffs::identity());
    designedSplit = designedLow = designedHigh = -1.0f;
    shelfDesignedFreq = -1.0;
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

    // 4.4 黙っている間の雑音: 下がるときはすぐ、上がるときはゆっくり
    if (! noiseValid || lowerDb < noiseDb)
        noiseDb = lowerDb;
    else
        noiseDb = std::min (lowerDb, noiseDb + tuning.noiseRiseDbPerSecond * frameLength / fs);
    noiseValid = true;

    // 4.3 声が出ていた間の平均（声が出ている区切りだけで更新する）
    const bool voiced = lowerDb >= noiseDb + tuning.voiceOverNoiseDb && lowerDb >= tuning.voiceFloorDb
                        && ratioNow <= tuning.vowelRatioDb;
    if (voiced)
    {
        if (! averageValid)
        {
            average = lower;
            averageValid = true;
        }
        else
        {
            const double a = std::exp (-(frameLength / fs) / tuning.averageSeconds);
            average = a * average + (1.0 - a) * lower;
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
    // 画面の「検出」の印: サ行らしく、上の帯域が黙っている間の雑音より十分大きい（無音の雑音で灯らないように）
    sibilant = sibilantLike && upperDb >= noiseDb + tuning.voiceOverNoiseDb;

    if (trace != nullptr)
        trace->push_back ({ static_cast<double> (frames * frameLength) / fs, static_cast<float> (upperDb), static_cast<float> (lowerDb),
                            static_cast<float> (averageDb), static_cast<float> (noiseDb), static_cast<float> (target),
                            static_cast<uint8_t> (voiced ? 1 : 0), static_cast<uint8_t> (sibilantLike ? 1 : 0) });
}

void DeEsser::process (float* data, int n)
{
    const double freqCoef = 1.0 - std::exp (-kShelfUpdateInterval / (kFreqSmoothSeconds * fs));
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
                shelf.setCoeffs (cutDb == 0.0 ? BiquadCoeffs::identity() : design::highShelf2 (shelfFreq, -cutDb, tuning.shelfQ, fs));
                shelfDesignedDb = cutDb;
                shelfDesignedFreq = shelfFreq;
            }
        }
        data[i] = static_cast<float> (shelf.process (x));
    }
}

} // namespace vcab
