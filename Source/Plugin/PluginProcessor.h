#pragma once

#include "Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>

// 画面のスペクトル用に、音の処理のスレッドから音を渡す輪（書き手 1、読み手 1。ロックしない。あふれた分は捨てる）
class SpectrumFifo
{
public:
    static constexpr int kSize = 1 << 15;

    void push (const float* x, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (x, x + n1, buffer.begin() + s1);
        std::copy (x + n1, x + n1 + n2, buffer.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }

    // 読めるだけ読む。返り値は読んだ数
    int pull (float* dst, int maxN)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        std::copy (buffer.begin() + s1, buffer.begin() + s1 + n1, dst);
        std::copy (buffer.begin() + s2, buffer.begin() + s2 + n2, dst + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    juce::AbstractFifo fifo { kSize };
    std::array<float, kSize> buffer {};
};

class VcabToneProcessor : public juce::AudioProcessor
{
public:
    VcabToneProcessor();
    ~VcabToneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // 入力が止まった後も、IIR（ローカットの 20Hz など）の余韻が少し残る
    double getTailLengthSeconds() const override { return 0.1; }
    // ホストのバイパスを、画面の「バイパス」と同じものにする（ホスト側で切っても中の判定が続き、10ms でなめらかに移る。VcabDenoise と同じ）
    juce::AudioProcessorParameter* getBypassParameter() const override { return param (vcab::P::bypass); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }
    juce::RangedAudioParameter* param (vcab::P id) const { return params[static_cast<size_t> (id)]; }
    float value (vcab::P id) const { return params[static_cast<size_t> (id)]->convertFrom0to1 (params[static_cast<size_t> (id)]->getValue()); }
    vcab::Settings currentSettings() const;
    void resetAllParameters(); // すべての設定値を初期値に戻す（入力チャンネルは戻さない）

    // 画面用
    vcab::EngineMeters& meters() { return engine.meters; }
    double sampleRate() const { return currentSampleRate.load(); }
    SpectrumFifo inputFifo, outputFifo;
    std::atomic<int> usedInputChannel { 0 }; // 自動のときに使っている入力チャンネル（0 = 1ch、1 = 2ch）
    // 削った分だけを聞く（確かめるための切り替え。設定には保存しない。6.3）
    std::atomic<bool> listenRemoved { false };

    // EXE 版だけ、外枠が入れる。音声の設定画面を開く（VST3 では空のまま）
    std::function<void()> openAudioSettings;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void mixToMono (const juce::AudioBuffer<float>& buffer, int start, int n, int choice); // 入力チャンネルの設定に従って mono に書く

    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, vcab::kNumParams> params {};
    std::atomic<double> currentSampleRate { 48000.0 };

    vcab::Engine engine;
    std::vector<float> mono;

    // 入力チャンネルの自動選択（VcabDenoise・VcabShift と同じ形）
    static constexpr double kAutoSmoothSeconds = 0.3;  // 各チャンネルの音量のならし
    static constexpr double kAutoSwitchSeconds = 2.0;  // もう片方が大きい状態がこれだけ続いたら切り替える
    static constexpr float kAutoSwitchRatio = 3.98f;   // 6dB（パワー）
    static constexpr double kAutoFadeSeconds = 0.010;  // 切り替えのつなぎ
    float autoPower[2] { 0.0f, 0.0f };
    int autoChannel = 0, autoPrev = 0;
    int otherLouderFor = 0;
    float autoFade = 1.0f; // 前のチャンネルから今のチャンネルへのつなぎ（1 で今だけ）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VcabToneProcessor)
};
