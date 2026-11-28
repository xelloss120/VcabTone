// 入出力のスペクトルに、EQ の特性の曲線と、ディエッサーの判定の帯域・削り量を重ねたグラフ（7 章）。
// 縦軸は左がスペクトルの大きさ（dB）、右が EQ の増減（dB）。
// EQ の 5 本の点は曲線の上に置き、ドラッグで動かす（ローカット・ハイカットは左右だけ、低域・中域・高域は上下左右。
// シェルフの点は増減が半分になる所なので、上下に動かした量の 2 倍が増減になる）。
// ホイールで、ベルは幅、シェルフは増減（カットは動かない）。ダブルクリックで、増減を 0 に（カットは周波数を初期値に）。
#pragma once

#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

class GraphView : public juce::Component,
                  public juce::SettableTooltipClient,
                  private juce::Timer
{
public:
    explicit GraphView (VcabToneProcessor&);
    ~GraphView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // 削り量の表示（メーターと同じ値。画面のタイマーで更新）
    float displayCutDb() const { return shownCut; }
    bool sibilantLit() const;

private:
    static constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder;
    static constexpr float kMinDb = -96.0f, kMaxDb = 0.0f;
    static constexpr float kEqRange = 12.0f; // 右の縦軸（±12dB）
    static constexpr float kMinHz = 20.0f;

    VcabToneProcessor& proc;
    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> window, inRing, outRing, inPow, outPow, inDb, outDb, pullBuf, work;
    int inRingPos = 0, outRingPos = 0;

    vcab::Settings settings;
    float shownCut = 0.0f;       // 削り量の表示（すぐ上がり、ゆっくり下がる）
    double sibilantUntil = 0.0;  // サ行の印を灯しておく時刻（ms）

    // 点: 0 ローカット、1 低域、2 中域、3 高域、4 ハイカット
    int dragPoint = -1, hoverPoint = -1;

    // 画素ごとの周波数（plotArea の左端から 1 画素ずつ。幅か標本化周波数が変わったら作り直す）。
    // スペクトルと曲線を描くたびに、画素ごとに pow を計算し直さないため
    std::vector<float> pixelHz;
    float pixelHzWidth = -1.0f, pixelHzX = 0.0f;
    double pixelHzFs = -1.0;
    void updatePixelHz (const juce::Rectangle<float>& a, double fs);

    // EQ の特性の曲線（設定と大きさが変わったときだけ計算し直す）
    juce::Path eqCurve;
    vcab::EqSettings eqCurveSettings;
    bool eqCurveValid = false;
    juce::Rectangle<float> eqCurveArea;
    double eqCurveFs = -1.0;

    // 開いている操作（ホストに伝える beginChangeGesture の数。ドラッグ中のダブルクリックやホイールで、二重に開かないように）
    std::array<int, vcab::kNumParams> gestureDepth {};

    void timerCallback() override;
    void computeSpectrum (const std::vector<float>& ring, int ringPos, std::vector<float>& power, std::vector<float>& db);

    juce::Rectangle<float> plotArea() const;
    float maxHz() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;
    float yForEq (float db) const;
    float eqForY (float y) const;

    static vcab::P freqParam (int i);
    static vcab::P gainParam (int i); // カットは count（増減なし）
    bool pointOn (int i) const;
    static vcab::P shapeParam (int i); // カットは count
    static vcab::P widthParam (int i); // カットは count
    bool isBell (int i) const;  // 低域・中域・高域の、いまの形
    bool isShelf (int i) const;
    juce::Point<float> pointPos (int i) const;
    int hitPoint (juce::Point<float> p) const;
    void setParam (vcab::P id, float v);
    void gesture (vcab::P id, bool start);
};
