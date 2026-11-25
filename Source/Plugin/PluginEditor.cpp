#include "PluginEditor.h"
#include "EditorStyle.h"

using namespace editor;

namespace
{
constexpr int kHeader = 44;      // 上の段: 名前、バイパス、（右から）設定値初期化、オーディオ設定、入力チャンネル
constexpr int kRow = 30;         // 操作の帯の 1 行
constexpr int kButtonWidth = 120;
constexpr int kInputComboWidth = 176; // VcabDenoise・VcabShift の入力チャンネルの選択欄と同じ幅
constexpr int kLabelInset = 5;        // Label の文字の左の余白（JUCE の標準）
constexpr int kLabelGap = 10;         // 名前と部品の隙間
constexpr int kTimerHz = 30;

// EQ の 5 列（ローカット、低域、中域、高域、ハイカット）の、行ごとのパラメータ（count は空き）
const vcab::P kEqGrid[5][3] = {
    { vcab::P::count, vcab::P::lowCutFreq, vcab::P::lowCutSlope },
    { vcab::P::lowGain, vcab::P::lowFreq, vcab::P::lowSlope },
    { vcab::P::midGain, vcab::P::midFreq, vcab::P::midWidth },
    { vcab::P::highGain, vcab::P::highFreq, vcab::P::highSlope },
    { vcab::P::count, vcab::P::highCutFreq, vcab::P::highCutSlope },
};
const char* const kEqRowNames[3] = { "増減", "周波数", "傾き・幅" };
const char* const kEqRowHelp[3] = {
    "低域・中域・高域を上げ下げする量（±6dB）。",
    "それぞれの帯域の位置。グラフの点を左右にドラッグしても動かせます。",
    "ローカット・低域・高域・ハイカットは傾き（6 / 12 / 24dB/oct）、中域は幅（オクターブ）。",
};
} // namespace

VcabToneEditor::Control& VcabToneEditor::add (vcab::P id, bool withLabel)
{
    const auto& info = vcab::paramInfo (id);
    auto& apvts = proc.state();
    auto c = std::make_unique<Control>();
    c->id = id;
    const auto help = utf8 (info.help);
    if (withLabel)
    {
        c->label.setText (utf8 (info.name), juce::dontSendNotification);
        c->label.setColour (juce::Label::textColourId, kText);
        c->label.setTooltip (help);
        addAndMakeVisible (c->label);
    }
    switch (info.type)
    {
        case vcab::ParamType::choice:
            c->combo = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < info.choices.size(); ++i)
                c->combo->addItem (utf8 (info.choices[i]), static_cast<int> (i) + 1);
            c->combo->setTooltip (help);
            addAndMakeVisible (*c->combo);
            if (id != vcab::P::inputChannel) // 入力チャンネルは「自動」の文字が変わるので、自前でつなぐ（timerCallback）
                c->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, info.key, *c->combo);
            break;
        case vcab::ParamType::toggle:
            c->toggle = std::make_unique<juce::ToggleButton> (utf8 (info.name));
            c->toggle->setTooltip (help);
            addAndMakeVisible (*c->toggle);
            c->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, info.key, *c->toggle);
            break;
        case vcab::ParamType::real:
            c->slider = std::make_unique<LimitedSlider>();
            c->slider->setTooltip (help);
            addAndMakeVisible (*c->slider);
            c->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, info.key, *c->slider);
            break;
    }
    controls.push_back (std::move (c));
    return *controls.back();
}

VcabToneEditor::VcabToneEditor (VcabToneProcessor& p)
    : AudioProcessorEditor (p), proc (p), graph (p)
{
    setLookAndFeel (&lookAndFeel);
    lookAndFeel.setTooltipAvoid (&graph, this); // グラフの説明は、グラフの外に出す

    // 上の段
    add (vcab::P::bypass, false);
    {
        auto& input = add (vcab::P::inputChannel, true);
        input.combo->onChange = [this, combo = input.combo.get()]
        {
            const int index = combo->getSelectedItemIndex();
            auto* param = proc.param (vcab::P::inputChannel);
            if (index < 0 || index == static_cast<int> (std::lround (proc.value (vcab::P::inputChannel))))
                return;
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
            param->endChangeGesture();
        };
    }

    // EQ の帯
    for (int r = 0; r < 3; ++r)
    {
        eqRowLabels[r].setText (utf8 (kEqRowNames[r]), juce::dontSendNotification);
        eqRowLabels[r].setColour (juce::Label::textColourId, kDim);
        eqRowLabels[r].setTooltip (utf8 (kEqRowHelp[r]));
        addAndMakeVisible (eqRowLabels[r]);
    }
    add (vcab::P::lowCutOn, false);
    add (vcab::P::highCutOn, false);
    const vcab::P titled[3] = { vcab::P::lowGain, vcab::P::midGain, vcab::P::highGain };
    for (int k = 0; k < 3; ++k)
    {
        eqTitles[k].setText (utf8 (vcab::paramInfo (titled[k]).name), juce::dontSendNotification);
        eqTitles[k].setColour (juce::Label::textColourId, kText);
        eqTitles[k].setTooltip (utf8 (vcab::paramInfo (titled[k]).help));
        addAndMakeVisible (eqTitles[k]);
    }
    for (const auto& column : kEqGrid)
        for (auto id : column)
            if (id != vcab::P::count)
                add (id, false);

    // ディエッサーの帯
    add (vcab::P::deessOn, false);
    for (auto id : { vcab::P::deessSplit, vcab::P::deessSensitivity, vcab::P::deessMaxCut, vcab::P::deessLow, vcab::P::deessHigh })
        add (id, true);
    // 判定の下端 < 境目 < 判定の上端（範囲から、ぶつかりうるのは境目と上端だけ）
    control (vcab::P::deessSplit)->slider->limit = [this] (double v)
    { return std::min (v, static_cast<double> (proc.value (vcab::P::deessHigh) / vcab::kDeessMinRatio)); };
    control (vcab::P::deessHigh)->slider->limit = [this] (double v)
    { return std::max (v, static_cast<double> (proc.value (vcab::P::deessSplit) * vcab::kDeessMinRatio)); };

    listenButton.setButtonText (utf8 ("削った分だけを聞く"));
    listenButton.setTooltip (utf8 ("ディエッサーが削った音だけを出します（何を削っているかを耳で確かめるため）。"
                                   "母音まで聞こえたら、削りすぎです（感度を上げるか、最大の削り量を下げます）。設定には保存しません。"));
    listenButton.setToggleState (proc.listenRemoved.load(), juce::dontSendNotification);
    listenButton.onClick = [this] { proc.listenRemoved.store (listenButton.getToggleState()); };
    addAndMakeVisible (listenButton);

    meter.setTooltip (utf8 ("いまディエッサーが削っている量（境目より上を下げる量）。"));
    addAndMakeVisible (meter);

    resetButton.setButtonText (utf8 ("設定値初期化"));
    resetButton.setTooltip (utf8 ("すべての設定を初期値に戻します（入力チャンネルは戻しません）。押すと確認が出ます。"));
    resetButton.onClick = [this]
    {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::QuestionIcon, utf8 ("設定値初期化"),
            utf8 ("すべての設定を初期値に戻します。よろしいですか？（入力チャンネルとオーディオデバイスの設定は変わりません）"),
            utf8 ("初期化"), utf8 ("キャンセル"), this);
        juce::Component::SafePointer<VcabToneEditor> safe (this);
        juce::AlertWindow::showAsync (options, [safe] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->proc.resetAllParameters();
        });
    };
    addAndMakeVisible (resetButton);

    // EXE 版だけ、オーディオの設定画面を開くボタン（「オプション」の中と同じ画面）
    audioSettingsButton.setButtonText (utf8 ("オーディオ設定"));
    audioSettingsButton.setTooltip (utf8 ("オーディオの設定画面（入出力の機器、サンプルレート、バッファの大きさ）を開きます。左上の「オプション」の中と同じ画面です。"));
    audioSettingsButton.onClick = [this]
    {
        if (proc.openAudioSettings)
            proc.openAudioSettings();
    };
    if (proc.openAudioSettings)
        addAndMakeVisible (audioSettingsButton);

    addAndMakeVisible (graph);
    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (kTimerHz);
}

VcabToneEditor::~VcabToneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

VcabToneEditor::Control* VcabToneEditor::control (vcab::P id)
{
    for (auto& c : controls)
        if (c->id == id)
            return c.get();
    return nullptr;
}

void VcabToneEditor::CutMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float maxDb = vcab::paramInfo (vcab::P::deessMaxCut).maxValue;
    const float cut = graph.displayCutDb();
    g.setFont (juce::FontOptions (kFont));
    g.setColour (kText);
    auto labelBox = r.removeFromLeft (70.0f);
    g.drawText (utf8 ("削り量"), labelBox.withTrimmedLeft (kLabelInset), juce::Justification::centredLeft);
    // 右端にサ行の検出の印
    auto mark = r.removeFromRight (22.0f);
    g.setColour (graph.sibilantLit() ? kSibilantColour : kSibilantColour.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (mark.getCentre()));
    r = r.reduced (0.0f, 4.0f);
    g.setColour (kGraph);
    g.fillRect (r);
    g.setColour (kCutColour);
    g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, cut / maxDb)));
    g.setColour (kText);
    g.drawText (juce::String (cut, 1) + " dB", r, juce::Justification::centred);
}

void VcabToneEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kText);
    g.setFont (juce::FontOptions (kFont + 3.0f, juce::Font::bold));
    g.drawText ("VcabTone", juce::Rectangle<int> (kMargin, 0, 120, kHeader), juce::Justification::centredLeft);
    g.setColour (kPanel);
    g.fillRoundedRectangle (eqArea.toFloat(), 4.0f);
    g.fillRoundedRectangle (deessArea.toFloat(), 4.0f);
}

void VcabToneEditor::resized()
{
    auto r = getLocalBounds().reduced (kMargin, 0);
    const juce::Font font { juce::FontOptions (kFont) };
    auto textWidth = [&font] (const juce::String& t) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, t)); };

    // 上の段: 名前（左）、バイパス。右端に寄せて、入力チャンネル、オーディオ設定、設定値初期化
    {
        auto h = r.removeFromTop (kHeader).reduced (0, 5);
        h.removeFromLeft (118);
        control (vcab::P::bypass)->toggle->setBounds (h.removeFromLeft (110));
        resetButton.setBounds (h.removeFromRight (kButtonWidth));
        h.removeFromRight (6);
        if (audioSettingsButton.isVisible())
        {
            audioSettingsButton.setBounds (h.removeFromRight (kButtonWidth));
            h.removeFromRight (10);
        }
        else
            h.removeFromRight (4);
        auto* input = control (vcab::P::inputChannel);
        input->combo->setBounds (h.removeFromRight (kInputComboWidth));
        input->label.setBounds (h.removeFromRight (textWidth (input->label.getText()) + kLabelInset + kLabelGap));
    }

    // EQ の帯: 見出しの行 ＋ 3 行（増減、周波数、傾き・幅）× 5 列
    eqArea = r.removeFromTop (kRow * 4 + 12);
    {
        auto s = eqArea.reduced (8, 6);
        int rowLabelW = 0;
        for (auto& l : eqRowLabels)
            rowLabelW = std::max (rowLabelW, textWidth (l.getText()));
        rowLabelW += kLabelInset + kLabelGap;
        auto labels = s.removeFromLeft (rowLabelW);
        labels.removeFromTop (kRow);
        for (auto& l : eqRowLabels)
            l.setBounds (labels.removeFromTop (kRow));

        const int gap = 8;
        const int colW = (s.getWidth() - gap * 4) / 5;
        for (int c = 0; c < 5; ++c)
        {
            auto col = s.removeFromLeft (colW);
            s.removeFromLeft (gap);
            auto title = col.removeFromTop (kRow).reduced (0, 3);
            if (c == 0)
                control (vcab::P::lowCutOn)->toggle->setBounds (title);
            else if (c == 4)
                control (vcab::P::highCutOn)->toggle->setBounds (title);
            else
                eqTitles[c - 1].setBounds (title);
            for (int row = 0; row < 3; ++row)
            {
                auto cell = col.removeFromTop (kRow).reduced (0, 3);
                const auto id = kEqGrid[c][row];
                if (id == vcab::P::count)
                    continue;
                auto* ctl = control (id);
                if (ctl->slider != nullptr)
                    ctl->slider->setBounds (cell);
                else
                    ctl->combo->setBounds (cell);
            }
        }
    }

    // ディエッサーの帯（2 行）: オン/オフ・削った分だけを聞く ｜ 境目・判定の下端 ｜ 感度・判定の上端 ｜ 最大の削り量・メーター
    r.removeFromTop (6);
    deessArea = r.removeFromTop (kRow * 2 + 12);
    {
        auto s = deessArea.reduced (8, 6);
        auto col0 = s.removeFromLeft (180);
        control (vcab::P::deessOn)->toggle->setBounds (col0.removeFromTop (kRow).reduced (0, 3));
        listenButton.setBounds (col0.removeFromTop (kRow).reduced (0, 3));
        s.removeFromLeft (8);

        auto labelW = [&] (std::initializer_list<vcab::P> ids)
        {
            int w = 0;
            for (auto id : ids)
                w = std::max (w, textWidth (control (id)->label.getText()));
            return w + kLabelInset + kLabelGap;
        };
        const int gap = 14;
        const int colW = (s.getWidth() - gap * 2) / 3;
        auto place = [this] (vcab::P id, juce::Rectangle<int> line, int lw)
        {
            auto* c = control (id);
            line = line.reduced (0, 3);
            c->label.setBounds (line.removeFromLeft (lw));
            c->slider->setBounds (line);
        };
        auto col1 = s.removeFromLeft (colW);
        s.removeFromLeft (gap);
        auto col2 = s.removeFromLeft (colW);
        s.removeFromLeft (gap);
        auto col3 = s;
        const int l1 = labelW ({ vcab::P::deessSplit, vcab::P::deessLow });
        const int l2 = labelW ({ vcab::P::deessSensitivity, vcab::P::deessHigh });
        place (vcab::P::deessSplit, col1.removeFromTop (kRow), l1);
        place (vcab::P::deessLow, col1.removeFromTop (kRow), l1);
        place (vcab::P::deessSensitivity, col2.removeFromTop (kRow), l2);
        place (vcab::P::deessHigh, col2.removeFromTop (kRow), l2);
        const int l3 = labelW ({ vcab::P::deessMaxCut });
        place (vcab::P::deessMaxCut, col3.removeFromTop (kRow), l3);
        meter.setBounds (col3.removeFromTop (kRow).reduced (0, 3));
    }

    // グラフ（残り全部）
    r.removeFromTop (6);
    graph.setBounds (r.removeFromTop (r.getHeight() - kMargin));
}

void VcabToneEditor::timerCallback()
{
    const auto s = proc.currentSettings();
    auto dim = [this] (vcab::P id, bool on)
    {
        if (auto* c = control (id))
        {
            for (juce::Component* comp : { static_cast<juce::Component*> (c->slider.get()), static_cast<juce::Component*> (c->combo.get()),
                                           static_cast<juce::Component*> (&c->label) })
                if (comp != nullptr)
                    comp->setAlpha (on ? 1.0f : 0.45f);
        }
    };
    // オフにした帯域の部品は薄くする（操作はできる）
    dim (vcab::P::lowCutFreq, s.eq.lowCutOn);
    dim (vcab::P::lowCutSlope, s.eq.lowCutOn);
    dim (vcab::P::highCutFreq, s.eq.highCutOn);
    dim (vcab::P::highCutSlope, s.eq.highCutOn);
    for (auto id : { vcab::P::deessSplit, vcab::P::deessSensitivity, vcab::P::deessMaxCut, vcab::P::deessLow, vcab::P::deessHigh })
        dim (id, s.deess.on);
    // バイパスのとき、削った分だけを聞くは効かない
    listenButton.setEnabled (! s.bypass && s.deess.on);
    meter.repaint();

    // 入力チャンネルの「自動」に、いま使っているチャンネルを添える
    if (auto* c = control (vcab::P::inputChannel))
    {
        const auto text = utf8 ("自動（") + (proc.usedInputChannel.load() == 1 ? "2" : "1") + utf8 ("ch を使用中）");
        const auto& info = vcab::paramInfo (vcab::P::inputChannel);
        auto* combo = c->combo.get();
        if (combo->getItemText (0) != text && ! combo->isPopupActive())
        {
            combo->clear (juce::dontSendNotification);
            combo->addItem (text, 1);
            for (size_t i = 1; i < info.choices.size(); ++i)
                combo->addItem (utf8 (info.choices[i]), static_cast<int> (i) + 1);
        }
        const int index = static_cast<int> (std::lround (proc.value (vcab::P::inputChannel)));
        if (combo->getSelectedItemIndex() != index)
            combo->setSelectedItemIndex (index, juce::dontSendNotification);
    }
}
