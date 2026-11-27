#include "Params.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vcab
{

namespace
{

// 周波数のつまみは、範囲の両端の相乗平均を真ん中に置く（対数の目盛りに近い動き）
float geoCentre (float lo, float hi) { return std::sqrt (lo * hi); }

// 範囲に収めた後の値から作る（表を作る途中の確かめでも使うので、表を見ない）
Settings settingsFromValuesInRange (const std::array<float, kNumParams>& v)
{
    auto get = [&v] (P id) { return v[static_cast<size_t> (id)]; };
    auto on = [&] (P id) { return get (id) >= 0.5f; };
    auto slope = [&] (P id) { return static_cast<Slope> (std::clamp (static_cast<int> (std::lround (get (id))), 0, 2)); };

    Settings s;
    s.bypass = on (P::bypass);
    auto& e = s.eq;
    e.lowCutOn = on (P::lowCutOn);
    e.lowCutFreq = get (P::lowCutFreq);
    e.lowCutSlope = slope (P::lowCutSlope);
    e.lowGain = get (P::lowGain);
    e.lowFreq = get (P::lowFreq);
    e.lowSlope = slope (P::lowSlope);
    e.midGain = get (P::midGain);
    e.midFreq = get (P::midFreq);
    e.midWidth = get (P::midWidth);
    e.highGain = get (P::highGain);
    e.highFreq = get (P::highFreq);
    e.highSlope = slope (P::highSlope);
    e.highCutOn = on (P::highCutOn);
    e.highCutFreq = get (P::highCutFreq);
    e.highCutSlope = slope (P::highCutSlope);

    auto& d = s.deess;
    d.on = on (P::deessOn);
    d.split = get (P::deessSplit);
    d.sensitivity = get (P::deessSensitivity);
    d.maxCut = get (P::deessMaxCut);
    d.low = get (P::deessLow);
    d.high = get (P::deessHigh);
    // 判定の下端 < 境目 < 判定の上端 を守る（画面ではつまみを止めるが、オートメーションや保存した値でも崩れないように。
    // つまみの値は書き換えない）。範囲から、ぶつかりうるのは境目と上端だけ（下端は最大 1kHz、境目は最小 3kHz）
    d.high = std::max (d.high, d.split * kDeessMinRatio);

    s.inputChannel = static_cast<InputChannel> (std::clamp (static_cast<int> (std::lround (get (P::inputChannel))), 0, 3));
    return s;
}

std::vector<ParamInfo> makeTable()
{
    using T = ParamType;
    std::vector<ParamInfo> t {
        { P::bypass, "bypass", "バイパス", T::toggle, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, "", {},
          "処理をせず、入力をそのまま出します（処理なしの音と比べるため）。中の判定は続けるので、戻してすぐに効きます。" },

        // EQ
        { P::lowCutOn, "low_cut_on", "ローカット", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "低い雑音（ハム、ファン、床の振動、吹かれ）を削ります。" },
        { P::lowCutFreq, "low_cut_freq", "ローカットの周波数", T::real, 20.0f, 300.0f, 80.0f, 1.0f, geoCentre (20.0f, 300.0f), "Hz", {},
          "これより下を削ります（ここで -3dB）。\n"
          "24dB/oct なら、声の高さ（基音）がこの 1.3 倍以上あれば、削れるのは 0.5dB 以下です。"
          "ボイチェンで声を高くした後なら、100〜120Hz に上げると、低い雑音（ハムなど）をより深く削れます。" },
        { P::lowCutSlope, "low_cut_slope", "ローカットの傾き", T::choice, 0.0f, 2.0f, 2.0f, 1.0f, 0.0f, "",
          { "6dB/oct", "12dB/oct", "24dB/oct" },
          "周波数より下を、どれだけ急に削るか。大きいほど、周波数より上を残したまま、下だけを深く削ります。" },
        { P::lowGain, "low_gain", "低域", T::real, -6.0f, 6.0f, 0.0f, 0.1f, 0.0f, "dB", {},
          "周波数より下を、まとめて上げ下げする量（下のシェルフ）。\n"
          "声の高さ（基音）を残したいときは、周波数を基音より下に置きます。" },
        { P::lowFreq, "low_freq", "低域の周波数", T::real, 100.0f, 1000.0f, 300.0f, 1.0f, geoCentre (100.0f, 1000.0f), "Hz", {},
          "低域のシェルフの位置（増減が半分になる所）。" },
        { P::lowSlope, "low_slope", "低域の傾き", T::choice, 0.0f, 2.0f, 2.0f, 1.0f, 0.0f, "",
          { "6dB/oct", "12dB/oct", "24dB/oct" },
          "低域のシェルフの、変わり目の急さ。大きいほど、周波数の近くだけで切り替わります。" },
        { P::midGain, "mid_gain", "中域", T::real, -6.0f, 6.0f, 0.0f, 0.1f, 0.0f, "dB", {},
          "周波数のまわりを上げ下げする量（ピーク）。声の明るさや、こもりの調整に使います。\n"
          "4kHz より上を上げると、サ行も強くなります（ディエッサーは EQ の後の音で判定します）。" },
        { P::midFreq, "mid_freq", "中域の周波数", T::real, 1000.0f, 8000.0f, 3000.0f, 1.0f, geoCentre (1000.0f, 8000.0f), "Hz", {},
          "中域のピークの真ん中。" },
        { P::midWidth, "mid_width", "中域の幅", T::real, 0.3f, 3.0f, 1.0f, 0.05f, geoCentre (0.3f, 3.0f), "oct", {},
          "中域のピークが効く幅（オクターブ。増減が半分になる所が両端）。\n"
          "例えば 3kHz・1 オクターブなら、約 2.1〜4.2kHz に効きます。" },
        { P::highGain, "high_gain", "高域", T::real, -6.0f, 6.0f, 0.0f, 0.1f, 0.0f, "dB", {},
          "周波数より上を、まとめて上げ下げする量（上のシェルフ）。空気感やシャリシャリの調整に使います。" },
        { P::highFreq, "high_freq", "高域の周波数", T::real, 6000.0f, 14000.0f, 8000.0f, 1.0f, geoCentre (6000.0f, 14000.0f), "Hz", {},
          "高域のシェルフの位置（増減が半分になる所）。" },
        { P::highSlope, "high_slope", "高域の傾き", T::choice, 0.0f, 2.0f, 2.0f, 1.0f, 0.0f, "",
          { "6dB/oct", "12dB/oct", "24dB/oct" },
          "高域のシェルフの、変わり目の急さ。大きいほど、周波数の近くだけで切り替わります。" },
        { P::highCutOn, "high_cut_on", "ハイカット", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "耳に痛い、ごく高い音を削ります。" },
        { P::highCutFreq, "high_cut_freq", "ハイカットの周波数", T::real, 10000.0f, 20000.0f, 16000.0f, 1.0f, geoCentre (10000.0f, 20000.0f), "Hz", {},
          "これより上を削ります（ここで -3dB）。" },
        { P::highCutSlope, "high_cut_slope", "ハイカットの傾き", T::choice, 0.0f, 2.0f, 2.0f, 1.0f, 0.0f, "",
          { "6dB/oct", "12dB/oct", "24dB/oct" },
          "周波数より上を、どれだけ急に削るか。" },

        // ディエッサー
        { P::deessOn, "deess_on", "ディエッサー", T::toggle, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", {},
          "サ行（さ、し、す、しゃ など）の強すぎる息の音を、その瞬間だけ下げます。\n"
          "境目より上の音と、声の本体（境目より下）の比で判定するので、声の大きさに左右されません。" },
        { P::deessSplit, "deess_split", "境目", T::real, 3000.0f, 10000.0f, 5000.0f, 1.0f, geoCentre (3000.0f, 10000.0f), "Hz", {},
          "判定で上と下に分ける周波数。削るのも、ここから上です（シェルフの、削り量が半分になる所）。\n"
          "下げすぎると母音（特に「い」）でも反応し、上げすぎると「す」「し」を取りこぼします。" },
        { P::deessSensitivity, "deess_sensitivity", "感度", T::real, -30.0f, 10.0f, -10.0f, 0.5f, 0.0f, "dB", {},
          "境目より上の音が、声の本体（直近の声の、境目より下の平均）よりどれだけ小さい所から削り始めるか。\n"
          "下げる（マイナスを大きくする）ほど、弱いサ行から削ります。" },
        { P::deessMaxCut, "deess_max_cut", "最大の削り量", T::real, 0.0f, 12.0f, 6.0f, 0.5f, 0.0f, "dB", {},
          "1 回に削る量の上限。削りすぎると、舌足らずに聞こえます。" },
        { P::deessLow, "deess_low", "判定の下端", T::real, 200.0f, 1000.0f, 500.0f, 1.0f, geoCentre (200.0f, 1000.0f), "Hz", {},
          "声の本体として見る帯域の下の端。声の高さ（基音）や低い雑音、ローカットの設定で、判定が揺れないようにするためのものです。" },
        { P::deessHigh, "deess_high", "判定の上端", T::real, 8000.0f, 20000.0f, 16000.0f, 1.0f, geoCentre (8000.0f, 20000.0f), "Hz", {},
          "サ行として見る帯域の上の端。判定は EQ の後の音で行うので、ハイカットより上にしても、そこにはほとんど音がありません。" },

        // 入力
        { P::inputChannel, "input_channel", "入力チャンネル", T::choice, 0.0f, 3.0f, 0.0f, 1.0f, 0.0f, "",
          { "自動", "1ch", "2ch", "平均" },
          "マイクの入力チャンネル。自動は、1ch と 2ch の大きいほうを使います（もう片方が 2 秒続けて 6dB 以上大きいと切り替え）。" },
    };

    // 表の並びと P の並びがそろっていることを確かめる（書き漏れ、並べ違いの防止）
    assert (static_cast<int> (t.size()) == kNumParams);
    for (int i = 0; i < kNumParams; ++i)
        assert (static_cast<int> (t[static_cast<size_t> (i)].id) == i);

    // Settings の仮の初期値が、表の初期値と同じことを確かめる（表だけ変えて、仮の値が古いまま残らないように）
    std::array<float, kNumParams> v {};
    for (const auto& p : t)
        v[static_cast<size_t> (p.id)] = p.defaultValue;
    const Settings a {};
    const Settings b = settingsFromValuesInRange (v);
    const auto& ea = a.eq;
    const auto& eb = b.eq;
    assert (a.bypass == b.bypass && a.inputChannel == b.inputChannel
            && ea.lowCutOn == eb.lowCutOn && ea.lowCutFreq == eb.lowCutFreq && ea.lowCutSlope == eb.lowCutSlope
            && ea.lowGain == eb.lowGain && ea.lowFreq == eb.lowFreq && ea.lowSlope == eb.lowSlope
            && ea.midGain == eb.midGain && ea.midFreq == eb.midFreq && ea.midWidth == eb.midWidth
            && ea.highGain == eb.highGain && ea.highFreq == eb.highFreq && ea.highSlope == eb.highSlope
            && ea.highCutOn == eb.highCutOn && ea.highCutFreq == eb.highCutFreq && ea.highCutSlope == eb.highCutSlope
            && a.deess.on == b.deess.on && a.deess.split == b.deess.split && a.deess.sensitivity == b.deess.sensitivity
            && a.deess.maxCut == b.deess.maxCut && a.deess.low == b.deess.low && a.deess.high == b.deess.high);
    (void) ea;
    (void) eb;
    return t;
}

} // namespace

const std::vector<ParamInfo>& paramTable()
{
    static const std::vector<ParamInfo> table = makeTable();
    return table;
}

const ParamInfo& paramInfo (P id)
{
    return paramTable()[static_cast<size_t> (id)];
}

int findParamByKey (const std::string& key)
{
    for (const auto& p : paramTable())
        if (key == p.key)
            return static_cast<int> (p.id);
    return -1;
}

bool isUiSetting (P id)
{
    return id == P::inputChannel;
}

std::array<float, kNumParams> defaultValues()
{
    std::array<float, kNumParams> v {};
    for (const auto& p : paramTable())
        v[static_cast<size_t> (p.id)] = p.defaultValue;
    return v;
}

Settings Settings::fromValues (const std::array<float, kNumParams>& v)
{
    // 値は表の範囲に収める（数でなければ初期値）。プラグインと処理ツールは範囲の中の値しか渡さないが、
    // ほかの入り口から使っても、幅 0 以下などの値で計算が壊れないように
    std::array<float, kNumParams> w {};
    for (const auto& p : paramTable())
    {
        const float x = v[static_cast<size_t> (p.id)];
        w[static_cast<size_t> (p.id)] = std::isnan (x) ? p.defaultValue : std::clamp (x, p.minValue, p.maxValue);
    }
    return settingsFromValuesInRange (w);
}

} // namespace vcab
