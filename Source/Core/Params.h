// パラメータの唯一の定義。プラグインの設定、処理ツール、画面は、どれもこの表から作る。
// JUCE には依存しない。
#pragma once

#include <array>
#include <string>
#include <vector>

namespace vcab
{

// パラメータの番号（ホストのオートメーションの一覧もこの順番）。Params.cpp の表もこの順番に並べる。
enum class P : int
{
    // 全体
    bypass,

    // EQ（3 章）
    lowCutOn, lowCutFreq, lowCutSlope,
    lowGain, lowFreq, lowSlope,
    midGain, midFreq, midWidth,
    highGain, highFreq, highSlope,
    highCutOn, highCutFreq, highCutSlope,

    // ディエッサー（4 章）
    deessOn,
    deessSplit,
    deessSensitivity,
    deessMaxCut,
    deessLow,
    deessHigh,

    // 入力（音の処理の設定ではないもの）
    inputChannel,

    count
};

constexpr int kNumParams = static_cast<int> (P::count);

enum class ParamType { real, toggle, choice };

struct ParamInfo
{
    P id;
    const char* key;      // 保存とホストの識別に使う名前（変えない）
    const char* name;     // 画面に出す名前
    ParamType type;
    float minValue, maxValue, defaultValue;
    float step;           // 0 なら連続
    float skewCentre;     // 0 なら直線。そうでなければ、つまみの真ん中に来る値
    const char* unit;
    std::vector<const char*> choices;
    const char* help;     // ツールチップの説明
};

// 傾き（6 / 12 / 24dB/oct）
enum class Slope : int { db6, db12, db24 };
// 入力チャンネル
enum class InputChannel : int { automatic, ch1, ch2, average };

const std::vector<ParamInfo>& paramTable();
const ParamInfo& paramInfo (P id);
int findParamByKey (const std::string& key); // 見つからなければ -1
bool isUiSetting (P id); // 入力チャンネルなど（オートメーションの対象にせず、設定値初期化でも戻さない）

// ディエッサーの判定の下端 < 境目 < 判定の上端 を守るときの、隣との最小の比（6.2）
constexpr float kDeessMinRatio = 1.25f;

// EQ の設定（3 章）
struct EqSettings
{
    bool lowCutOn = true;
    float lowCutFreq = 80.0f;
    Slope lowCutSlope = Slope::db24;
    float lowGain = 0.0f, lowFreq = 300.0f;
    Slope lowSlope = Slope::db24;
    float midGain = 0.0f, midFreq = 3000.0f, midWidth = 1.0f; // 幅はオクターブ
    float highGain = 0.0f, highFreq = 8000.0f;
    Slope highSlope = Slope::db24;
    bool highCutOn = true;
    float highCutFreq = 16000.0f;
    Slope highCutSlope = Slope::db24;
};

// ディエッサーの設定（4 章）
struct DeessSettings
{
    bool on = true;
    float split = 5000.0f;       // 境目（Hz）
    float sensitivity = -10.0f;  // しきい値（dB。平均との比がこれを超えた分に応じて削る）
    float maxCut = 6.0f;         // 最大の削り量（dB）
    float low = 500.0f;          // 判定の下端（Hz）
    float high = 16000.0f;       // 判定の上端（Hz）
};

// 処理に渡す設定（パラメータの値を型の付いた形にしたもの）。
// ここの初期値は、fromValues で上書きするまでの仮の値（表の初期値と同じにしておく。Params.cpp の assert で確かめている）
struct Settings
{
    bool bypass = false;
    EqSettings eq;
    DeessSettings deess;
    InputChannel inputChannel = InputChannel::automatic;

    static Settings fromValues (const std::array<float, kNumParams>& v);
};

std::array<float, kNumParams> defaultValues();

} // namespace vcab
