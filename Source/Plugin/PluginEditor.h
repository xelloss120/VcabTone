#pragma once

#include "GraphView.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

class VcabToneEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit VcabToneEditor (VcabToneProcessor&);
    ~VcabToneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 800, kHeight = 600;

private:
    // ドラッグで動かせる値を、隣のつまみの所で止めるバー（判定の下端 < 境目 < 判定の上端。6.2）
    class LimitedSlider : public juce::Slider
    {
    public:
        LimitedSlider() : juce::Slider (juce::Slider::LinearBar, juce::Slider::TextBoxLeft) {}
        std::function<double (double)> limit;
        double snapValue (double attempted, DragMode mode) override
        {
            const double v = juce::Slider::snapValue (attempted, mode);
            return limit ? limit (v) : v;
        }
    };

    struct Control
    {
        vcab::P id;
        juce::Label label;
        std::unique_ptr<LimitedSlider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    // 削り量のメーター（ディエッサーの帯の中）
    class CutMeter : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit CutMeter (GraphView& g) : graph (g) {}
        void paint (juce::Graphics&) override;
        int labelWidth = 70; // 名前の欄の幅（上の「最大の削り量」の名前の欄と同じにする）
    private:
        GraphView& graph;
    };

    VcabToneProcessor& proc;
    VCLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltip { this, 600 };
    std::vector<std::unique_ptr<Control>> controls;
    GraphView graph;
    CutMeter meter { graph };
    juce::TextButton resetButton, audioSettingsButton;
    juce::ToggleButton listenButton;
    juce::Label eqRowLabels[3];
    juce::Label eqTitles[3]; // 低域・中域・高域の見出し（カットはオン/オフのチェックボックスが見出し）
    juce::Rectangle<int> eqArea, deessArea; // 操作の帯（paint で下地を描く）

    Control* control (vcab::P id);
    Control& add (vcab::P id, bool withLabel);
    void timerCallback() override;
};
