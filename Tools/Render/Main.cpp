// 開発用の WAV の処理ツール。
//   vcabtn_render 入力.wav 出力.wav [キー=値 ...] [--block n] [--listen] [--trace 出力.csv] [--response 出力.csv]
// キーは Params.cpp の key（例: low_cut_freq=120 mid_gain=2 deess_sensitivity=-15）。値は表の範囲（つまみの範囲）に収める（収めたときは知らせる）。
// 入力が 2 チャンネル以上のときは、input_channel の 1ch（初期値の「自動」も 1ch）/ 2ch / 平均 で 1 チャンネルにする。
// --block n: 1 回に処理する標本の数（初期値 256）。
// --listen: 削った音だけを聞く。
// --trace: ディエッサーの区切り（約 1ms）ごとの { 時刻, 上の帯域, 下の帯域, 平均, 雑音, 上の帯域（10ms でならした値）, 下の帯域（同）, 目標の削り量, 声, サ行らしい } を CSV に書く（dB）。
// --response: その設定の EQ の特性（周波数と dB）を CSV に書く（画面の曲線と同じ計算）。--set-at で変える前の、最初の設定の特性。
// --set-at 秒 キー=値: その時刻から設定を変える（複数可。つまみを動かしたときのなめらかさを確かめるため）。
// 出力は 1 チャンネル、32 ビット浮動小数（測るときに丸めの差が乗らないように）。
#include "Engine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

static int run (int argc, char** argv);

static void usage()
{
    std::printf ("usage: vcabtn_render in.wav out.wav [key=value ...] [--block n] [--listen] [--trace out.csv] [--response out.csv] [--set-at sec key=value]\n");
}

// 数の読めない引数（std::stoi / std::stof の例外）は、使い方を出して終える
static int runChecked (int argc, char** argv)
{
    try
    {
        return run (argc, argv);
    }
    catch (const std::logic_error& e)
    {
        std::printf ("bad argument (%s)\n", e.what());
        usage();
        return 1;
    }
}

static int run (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 1;
    }

    auto values = vcab::defaultValues();
    int block = 256;
    bool listen = false;
    std::string tracePath, responsePath;
    struct Change { double at; int id; float value; };
    std::vector<Change> changes;
    // キー=値を読む（範囲に収める）。読めなければ -1
    auto parseKeyValue = [] (const std::string& a, float& out) -> int
    {
        const auto eq = a.find ('=');
        const int id = eq == std::string::npos ? -1 : vcab::findParamByKey (a.substr (0, eq));
        if (id < 0)
            return -1;
        const auto& info = vcab::paramInfo (static_cast<vcab::P> (id));
        float v = std::stof (a.substr (eq + 1));
        if (std::isnan (v))
            v = info.defaultValue;
        out = std::clamp (v, info.minValue, info.maxValue);
        if (out != v)
            std::printf ("%s: %g -> %g (range %g..%g)\n", info.key, v, out, info.minValue, info.maxValue);
        return id;
    };
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--block" && i + 1 < argc)
        {
            block = std::max (1, std::stoi (argv[++i]));
            continue;
        }
        if (a == "--listen")
        {
            listen = true;
            continue;
        }
        if (a == "--trace" && i + 1 < argc)
        {
            tracePath = argv[++i];
            continue;
        }
        if (a == "--response" && i + 1 < argc)
        {
            responsePath = argv[++i];
            continue;
        }
        if (a == "--set-at" && i + 2 < argc)
        {
            const double at = std::stod (argv[++i]);
            float v = 0.0f;
            const int id = parseKeyValue (argv[++i], v);
            if (id < 0)
            {
                std::printf ("unknown argument: %s\n", argv[i]);
                usage();
                return 1;
            }
            changes.push_back ({ at, id, v });
            continue;
        }
        float v = 0.0f;
        const int id = parseKeyValue (a, v);
        if (id < 0)
        {
            std::printf ("unknown argument: %s\n", a.c_str());
            usage();
            return 1;
        }
        values[static_cast<size_t> (id)] = v;
    }
    const auto settings = vcab::Settings::fromValues (values);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File inFile { juce::String (juce::CharPointer_UTF8 (argv[1])) };
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("cannot read %s\n", argv[1]);
        return 1;
    }
    if (reader->lengthInSamples > std::numeric_limits<int>::max() - 1000000)
    {
        std::printf ("too long: %s\n", argv[1]);
        return 1;
    }
    const int numCh = static_cast<int> (reader->numChannels);
    const int len = static_cast<int> (reader->lengthInSamples);
    const double fs = reader->sampleRate;
    juce::AudioBuffer<float> input (numCh, len);
    reader->read (&input, 0, len, 0, true, true);

    // 1 チャンネルにする
    std::vector<float> data (static_cast<size_t> (len), 0.0f);
    {
        const auto ch = settings.inputChannel;
        const float* c0 = input.getReadPointer (0);
        const float* c1 = input.getReadPointer (numCh > 1 ? 1 : 0);
        for (int i = 0; i < len; ++i)
            data[static_cast<size_t> (i)] = ch == vcab::InputChannel::ch2       ? c1[i]
                                            : ch == vcab::InputChannel::average ? 0.5f * (c0[i] + c1[i])
                                                                                : c0[i];
    }

    vcab::Engine engine;
    std::vector<vcab::DeEsser::TraceRow> trace;
    if (! tracePath.empty())
    {
        trace.reserve (static_cast<size_t> (len / (0.001 * fs)) + 16);
        engine.deEsser().setTrace (&trace);
    }
    engine.setSettings (settings);
    engine.setListenRemoved (listen);
    engine.prepare (fs, block);

    std::vector<double> blockSecs;
    blockSecs.reserve (static_cast<size_t> (len / block) + 2);
    // プラグインと同じく、非正規化数を 0 として扱う
    juce::ScopedNoDenormals noDenormals;
    const auto start = std::chrono::steady_clock::now();
    std::sort (changes.begin(), changes.end(), [] (const Change& x, const Change& y) { return x.at < y.at; });
    size_t nextChange = 0;
    for (int pos = 0; pos < len;)
    {
        // 設定を変える時刻で区切る（プラグインと同じく、ブロックの頭で設定を渡す）
        int n = std::min (block, len - pos);
        bool changed = false;
        while (nextChange < changes.size() && changes[nextChange].at * fs <= pos)
        {
            values[static_cast<size_t> (changes[nextChange].id)] = changes[nextChange].value;
            ++nextChange;
            changed = true;
        }
        if (changed)
        {
            const auto s2 = vcab::Settings::fromValues (values);
            engine.setSettings (s2);
        }
        if (nextChange < changes.size())
            n = std::min (n, std::max (1, static_cast<int> (std::ceil (changes[nextChange].at * fs)) - pos));
        const auto t0 = std::chrono::steady_clock::now();
        engine.process (data.data() + pos, n);
        blockSecs.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        pos += n;
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    // 一時ファイルに書いてから置き換える（書けなかったときに、前の出力を消してしまわないように）
    const juce::File outFile { juce::String (juce::CharPointer_UTF8 (argv[2])) };
    juce::WavAudioFormat wav;
    {
        juce::TemporaryFile tmpOut (outFile);
        bool written = false;
        {
            std::unique_ptr<juce::OutputStream> stream = tmpOut.getFile().createOutputStream();
            const auto options = juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (1).withBitsPerSample (32)
                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream, options) : nullptr);
            if (writer != nullptr)
            {
                const float* p = data.data();
                written = writer->writeFromFloatArrays (&p, 1, len);
            }
        }
        if (! written || ! tmpOut.overwriteTargetFileWithTemporary())
        {
            std::printf ("cannot write %s\n", argv[2]);
            return 1;
        }
    }

    if (! tracePath.empty())
    {
        std::ofstream tl (tracePath);
        tl << "t,upper,lower,average,noise,upper10,lower10,target,voiced,sibilant\n";
        for (const auto& r : trace)
            tl << r.time << ',' << r.upperDb << ',' << r.lowerDb << ',' << r.averageDb << ',' << r.noiseDb << ',' << r.upperGateDb << ','
               << r.lowerGateDb << ',' << r.targetDb << ','
               << static_cast<int> (r.voiced) << ',' << static_cast<int> (r.sibilant) << '\n';
    }
    if (! responsePath.empty())
    {
        std::ofstream rl (responsePath);
        rl << "f,db\n";
        if (! changes.empty())
            std::printf ("response: the settings before --set-at\n");
        const auto chain = vcab::Eq::design (settings.eq, fs);
        for (int k = 0; k <= 400; ++k)
        {
            const double f = 10.0 * std::pow (2.0, k / 400.0 * std::log2 (0.5 * fs / 10.0));
            rl << f << ',' << vcab::Eq::responseDb (chain, f, fs) << '\n';
        }
    }

    std::printf ("cpu %.3f%% of realtime\n", 100.0 * secs / (len / fs));
    if (! blockSecs.empty())
    {
        std::sort (blockSecs.begin(), blockSecs.end());
        const double blockLen = block / fs;
        std::printf ("block max %.1f us (%.1f%% of block), %zu blocks\n", blockSecs.back() * 1.0e6, 100.0 * blockSecs.back() / blockLen,
                     blockSecs.size());
    }
    return 0;
}

// Windows ではコマンドラインの引数を UTF-8 にして渡す（日本語のファイル名を読めるように）
#ifdef _WIN32
int wmain (int argc, wchar_t** wargv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back (juce::String (wargv[i]).toStdString());
    std::vector<char*> ptrs;
    for (auto& a : args)
        ptrs.push_back (a.data());
    return runChecked (argc, ptrs.data());
}
#else
int main (int argc, char** argv)
{
    return runChecked (argc, argv);
}
#endif
