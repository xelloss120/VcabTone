// ディエッサー（4 章）。境目より上の帯域と下の帯域の強さを約 1ms ごとに測り、
//   1. その瞬間の上下の比でサ行らしいかを決め、
//   2. 上の帯域を「声が出ていた間の、下の帯域の平均」と比べた値が感度を超えた分だけ、境目から上を上のシェルフで下げる。
// 先読みはしない（遅延 0）。
#pragma once

#include "../Params.h"
#include "Biquad.h"

#include <array>
#include <cstdint>
#include <vector>

namespace vcab
{

class DeEsser
{
public:
    // 内部の値（6.4）
    struct Tuning
    {
        double frameSeconds = 0.001;     // 判定の区切り
        double averageSeconds = 0.3;     // 平均の時定数（声が出ている間だけ進む）
        double voiceOverNoiseDb = 12.0;  // 声が出ている条件: 下の帯域が雑音よりこれだけ大きい
        double voiceFloorDb = -80.0;     // 声が出ている条件: 下の帯域の絶対的な下限（dBFS）
        double vowelRatioDb = -6.0;      // 母音らしさの線: 上 − 下 がこれ以下
        double sibilantRatioDb = -3.0;   // サ行らしさの線: 上 − 下 がこれより上
        double noiseRiseDbPerSecond = 3.0; // 雑音の追いかけ: 上がるときの速さ（下がるときはすぐ）
        double slope = 0.5;              // 削り量の傾き（感度を超えた分の何倍を削るか）
        double attackSeconds = 0.00033;  // 削り量の立ち上がりの時定数（約 1ms で 95%）
        double releaseSeconds = 0.02;    // 削り量の戻りの時定数（約 60ms で 95%）
        double shelfQ = design::kButterworth2Q; // 削るシェルフ（2 次、12dB/oct）の Q
    };

    // 区切りごとの記録（処理ツールの --trace 用）
    struct TraceRow
    {
        double time;      // 区切りの終わりの時刻（秒）
        float upperDb, lowerDb, averageDb, noiseDb; // 平均がまだないときの averageDb は NaN
        float targetDb;   // 目標の削り量
        uint8_t voiced, sibilant;
    };

    void setTuning (const Tuning& t) { tuning = t; }
    void prepare (double fs);
    void setSettings (const DeessSettings& s);

    // data: 判定に使う音（EQ の後）を受け取り、削った後の音に書き換える
    void process (float* data, int n);

    // 画面用（音の処理のスレッドが書き、画面のスレッドが読む値の元。Engine が受け渡す）
    float currentCutDb() const { return static_cast<float> (cutDb); }
    float blockPeakCutDb() const { return static_cast<float> (blockPeakCut); } // 最後の process の中の削り量の最大
    bool blockSibilant() const { return blockSibilantSeen; }                   // 最後の process の中に「検出」の区切りがあったか
    double split() const { return shelfFreq; }

    void setTrace (std::vector<TraceRow>* t) { trace = t; }

private:
    Tuning tuning;
    DeessSettings settings;
    double fs = 48000.0;

    // 判定の帯域（上: 境目の 4 次のハイパス ＋ 上端の 2 次のローパス、下: 下端の 4 次のハイパス ＋ 境目の 4 次のローパス）
    std::array<Biquad, 3> upperFilters;
    std::array<Biquad, 4> lowerFilters;
    float designedSplit = -1.0f, designedLow = -1.0f, designedHigh = -1.0f;
    void designDetectors();

    int frameLength = 48, inFrame = 0;
    double upperSum = 0.0, lowerSum = 0.0;
    double noiseDb = 0.0;
    bool noiseValid = false;
    double average = 0.0; // 下の帯域の平均（2 乗の平均）
    bool averageValid = false;
    double targetDb = 0.0, cutDb = 0.0;
    double attackCoef = 0.0, releaseCoef = 0.0;
    bool sibilant = false;
    double blockPeakCut = 0.0;
    bool blockSibilantSeen = false;
    int64_t frames = 0;

    // 削るシェルフ（境目から上）。削り量か位置が変わったら作り直す
    Biquad shelf;
    double shelfFreq = 5000.0, shelfTargetFreq = 5000.0, shelfDesignedDb = 0.0, shelfDesignedFreq = -1.0;
    int untilShelfUpdate = 0;

    std::vector<TraceRow>* trace = nullptr;

    void endFrame();
};

} // namespace vcab
