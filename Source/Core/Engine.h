// 全体の処理（2 章）: EQ → ディエッサー → 「削った音だけを聞く」とバイパスのクロスフェード。
// 入力チャンネルの選択はプラグインの側（PluginProcessor::mixToMono。VcabDenoise と同じ形）で行い、ここには 1 チャンネルを渡す。
// JUCE には依存しない。遅延は 0。
#pragma once

#include "Dsp/DeEsser.h"
#include "Dsp/Eq.h"
#include "Params.h"

#include <atomic>
#include <vector>

namespace vcab
{

// 画面用の値（音の処理のスレッドが書き、画面のスレッドが読む）
struct EngineMeters
{
    std::atomic<float> cutPeakDb { 0.0f };   // 前に読んでからの、削り量の最大（画面が 0 に戻す）
    std::atomic<bool> sibilantSeen { false }; // 前に読んでから、サ行らしい区切りがあったか（画面が false に戻す）
};

class Engine
{
public:
    void prepare (double fs, int maxBlock);
    void setSettings (const Settings& s);
    // 削った音だけを聞く（設定には保存しない。6.3）
    void setListenRemoved (bool on) { listenRemoved = on; }

    // data: 入力（1 チャンネル）を受け取り、出力に書き換える
    void process (float* data, int n);

    EngineMeters meters;
    DeEsser& deEsser() { return deesser; }
    double sampleRate() const { return fs; }

private:
    double fs = 48000.0;
    int maxBlock = 0;
    Settings settings;
    bool listenRemoved = false;
    Eq eq;
    DeEsser deesser;
    std::vector<float> dry, eqOut;
    float bypassMix = 0.0f, listenMix = 0.0f, mixStep = 0.002f;

    void processChunk (float* data, int n);
};

} // namespace vcab
