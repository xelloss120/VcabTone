#include "Engine.h"

#include <algorithm>

namespace vcab
{

namespace
{
constexpr double kMixSeconds = 0.010; // バイパスと「削った音だけを聞く」の切り替え（VcabDenoise と同じ 10ms）
}

void Engine::prepare (double sampleRate, int block)
{
    fs = sampleRate;
    maxBlock = std::max (block, 16);
    dry.assign (static_cast<size_t> (maxBlock), 0.0f);
    eqOut.assign (static_cast<size_t> (maxBlock), 0.0f);
    eq.prepare (fs);
    deesser.prepare (fs);
    eq.setTarget (settings.eq);
    deesser.setSettings (settings.deess);
    mixStep = static_cast<float> (1.0 / (kMixSeconds * fs));
    bypassMix = settings.bypass ? 1.0f : 0.0f;
    listenMix = listenRemoved ? 1.0f : 0.0f;
}

void Engine::setSettings (const Settings& s)
{
    settings = s;
    eq.setTarget (s.eq);
    deesser.setSettings (s.deess);
}

void Engine::process (float* data, int n)
{
    // 準備した大きさより大きな塊は区切って処理する（音の処理のスレッドで確保しないように）
    for (int done = 0; done < n;)
    {
        const int len = std::min (n - done, maxBlock);
        processChunk (data + done, len);
        done += len;
    }
}

void Engine::processChunk (float* data, int n)
{
    std::copy (data, data + n, dry.begin());

    // EQ → ディエッサー（判定は EQ の後の音。4.6）。バイパスの間も止めない（戻したとき、すぐに効くように）
    eq.process (data, n);
    std::copy (data, data + n, eqOut.begin());
    deesser.process (data, n);

    const float bypassTarget = settings.bypass ? 1.0f : 0.0f;
    const float listenTarget = listenRemoved ? 1.0f : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto si = static_cast<size_t> (i);
        listenMix += std::clamp (listenTarget - listenMix, -mixStep, mixStep);
        bypassMix += std::clamp (bypassTarget - bypassMix, -mixStep, mixStep);
        // 削った分 = ディエッサーの前 − 後（削っていなければ 0）
        const float processed = data[i];
        const float removed = eqOut[si] - processed;
        float out = processed + (removed - processed) * listenMix;
        out += (dry[si] - out) * bypassMix;
        data[i] = out;
    }
    // 画面用（このブロックの中の最大と、サ行らしい区切りがあったか）
    const float cutPeak = deesser.blockPeakCutDb();
    float prev = meters.cutPeakDb.load (std::memory_order_relaxed);
    while (cutPeak > prev && ! meters.cutPeakDb.compare_exchange_weak (prev, cutPeak, std::memory_order_relaxed)) {}
    if (deesser.blockSibilant())
        meters.sibilantSeen.store (true, std::memory_order_relaxed);
}

} // namespace vcab
