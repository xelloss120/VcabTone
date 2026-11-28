#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout VcabToneProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : vcab::paramTable())
    {
        const juce::ParameterID id { p.key, 1 };
        const bool automatable = ! vcab::isUiSetting (p.id);
        switch (p.type)
        {
            case vcab::ParamType::real:
            {
                juce::NormalisableRange<float> range (p.minValue, p.maxValue, p.step);
                if (p.skewCentre > 0.0f)
                    range.setSkewForCentre (p.skewCentre);
                const auto unit = u8 (p.unit);
                const int decimals = p.step >= 1.0f ? 0 : (p.step < 0.1f ? 2 : 1);
                const bool hz = juce::String (p.unit) == "Hz";
                const bool signedValue = p.minValue < 0.0f; // 増減と感度は + を付ける
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, u8 (p.name), range, p.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
                        [decimals, unit, hz, signedValue] (float v, int)
                        {
                            // 1kHz 以上は kHz で出す（打ち込みは Hz の数で読む）
                            if (hz && v >= 1000.0f)
                                return juce::String (v / 1000.0f, 2) + " kHz";
                            const auto number = juce::String (v, decimals);
                            return ((v > 0.0f && signedValue) ? "+" : "") + number + " " + unit;
                        })
                        .withValueFromStringFunction ([hz] (const juce::String& text)
                        {
                            const float v = text.getFloatValue();
                            return hz && text.containsIgnoreCase ("k") ? v * 1000.0f : v;
                        })));
                break;
            }
            case vcab::ParamType::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    id, u8 (p.name), p.defaultValue >= 0.5f, juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
                break;
            case vcab::ParamType::choice:
            {
                juce::StringArray choices;
                for (const auto* c : p.choices)
                    choices.add (u8 (c));
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    id, u8 (p.name), choices, static_cast<int> (p.defaultValue), juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
                break;
            }
        }
    }
    return layout;
}

VcabToneProcessor::VcabToneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VcabTone", createLayout())
{
    for (const auto& p : vcab::paramTable())
        params[static_cast<size_t> (p.id)] = apvts.getParameter (p.key);
}

bool VcabToneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (in) && ok (out);
}

vcab::Settings VcabToneProcessor::currentSettings() const
{
    std::array<float, vcab::kNumParams> v {};
    for (int i = 0; i < vcab::kNumParams; ++i)
        v[static_cast<size_t> (i)] = value (static_cast<vcab::P> (i));
    return vcab::Settings::fromValues (v);
}

void VcabToneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    const int block = juce::jmax (samplesPerBlock, 16);
    engine.setSettings (currentSettings());
    engine.setListenRemoved (listenRemoved.load());
    engine.prepare (sampleRate, block);
    mono.assign (static_cast<size_t> (block), 0.0f);
    setLatencySamples (0);
}

void VcabToneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (total == 0 || mono.empty())
        return;

    const auto settings = currentSettings();
    engine.setSettings (settings);
    engine.setListenRemoved (listenRemoved.load());

    // prepareToPlay で申告されたより大きな塊が来たときは、mono の大きさごとに区切る（音の処理のスレッドで確保しないように）
    const int step = static_cast<int> (mono.size());
    for (int start = 0; start < total; start += step)
    {
        const int n = std::min (step, total - start);
        mixToMono (buffer, start, n, static_cast<int> (settings.inputChannel));
        inputFifo.push (mono.data(), n);
        engine.process (mono.data(), n);
        outputFifo.push (mono.data(), n);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, start, mono.data(), n);
    }
}

void VcabToneProcessor::mixToMono (const juce::AudioBuffer<float>& buffer, int start, int n, int choice)
{
    // 入力チャンネルを選ぶ（マイクが 2ch 側に入っているオーディオインターフェースもあるため。VcabDenoise 1.2.5 と同じ）
    // choice: 0 = 自動、1 = 1ch、2 = 2ch、3 = 平均
    const int numIn = getTotalNumInputChannels();
    auto* out = mono.data();
    if (numIn <= 0)
    {
        std::fill (out, out + n, 0.0f);
        return;
    }
    const float* in[2] = { buffer.getReadPointer (0, start), buffer.getReadPointer (numIn >= 2 ? 1 : 0, start) };
    if (numIn < 2 || choice == 1)
    {
        std::copy (in[0], in[0] + n, out);
        return;
    }
    if (choice == 2)
    {
        std::copy (in[1], in[1] + n, out);
        return;
    }
    if (choice == 3)
    {
        for (int i = 0; i < n; ++i)
            out[i] = 0.5f * (in[0][i] + in[1][i]);
        return;
    }

    // 自動: 大きいほうを使う。各チャンネルの音量を 0.3 秒でならし、もう片方が 2 秒続けて 6dB 以上大きいと、10ms でつないで切り替える
    // （VcabDenoise・VcabShift と同じ形）
    const double fs = getSampleRate();
    const float a = static_cast<float> (std::exp (-n / (kAutoSmoothSeconds * fs)));
    for (int c = 0; c < 2; ++c)
    {
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            e += static_cast<double> (in[c][i]) * in[c][i];
        autoPower[c] = a * autoPower[c] + (1.0f - a) * static_cast<float> (e / n);
    }
    const int other = 1 - autoChannel;
    if (autoPower[other] > autoPower[autoChannel] * kAutoSwitchRatio && autoPower[other] > 1.0e-10f)
    {
        otherLouderFor += n;
        if (otherLouderFor >= kAutoSwitchSeconds * fs)
        {
            autoPrev = autoChannel;
            autoChannel = other;
            autoFade = 0.0f;
            otherLouderFor = 0;
        }
    }
    else
        otherLouderFor = 0;

    const float fadeStep = static_cast<float> (1.0 / (kAutoFadeSeconds * fs));
    for (int i = 0; i < n; ++i)
    {
        if (autoFade < 1.0f)
        {
            out[i] = autoFade * in[autoChannel][i] + (1.0f - autoFade) * in[autoPrev][i];
            autoFade = std::min (1.0f, autoFade + fadeStep);
        }
        else
            out[i] = in[autoChannel][i];
    }
    usedInputChannel.store (autoChannel);
}

juce::AudioProcessorEditor* VcabToneProcessor::createEditor()
{
    return new VcabToneEditor (*this);
}

void VcabToneProcessor::resetAllParameters()
{
    // 入力チャンネルは環境（オーディオインターフェース）に合わせた設定なので、戻さない
    for (const auto& p : vcab::paramTable())
    {
        if (vcab::isUiSetting (p.id))
            continue;
        // ホストに 1 回の操作として伝える（オートメーションの書き込みや元に戻すで、値が飛ばないように）
        auto* ranged = param (p.id);
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (ranged->getDefaultValue());
        ranged->endChangeGesture();
    }
}

void VcabToneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VcabToneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            // 前の版で保存した設定にないパラメータ（後から足した、低域・中域・高域の形など）は、初期値で補う。
            // JUCE は、ないパラメータを今の値のまま残すので、補わないと保存したときと違う音になる
            for (const auto& p : vcab::paramTable())
                if (! tree.getChildWithProperty ("id", p.key).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", p.key, nullptr);
                    child.setProperty ("value", p.defaultValue, nullptr);
                    tree.appendChild (child, nullptr);
                }
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VcabToneProcessor();
}
