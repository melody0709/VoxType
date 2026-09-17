#include "batch_vad_trimmer.h"

#include "config_store.h"
#include "vad_trim_core.h"
#include "utils.h"

#include <algorithm>
#include <cstdint>

namespace {

constexpr size_t kFrameBytes = 640; // 20ms at 16kHz, 16-bit, mono.

bool DetectVoice(const Config& config,
                 IVadDetector& engine,
                 const std::vector<BYTE>& frameData) {
    if (frameData.size() < sizeof(int16_t)) return false;

    const auto* samples = reinterpret_cast<const int16_t*>(frameData.data());
    const size_t sampleCount = frameData.size() / sizeof(int16_t);
    std::vector<float> floatBuf(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
        floatBuf[i] = static_cast<float>(samples[i]) / 32768.0f;
    }

    return engine.DetectSpeech(floatBuf.data(), floatBuf.size(), config.vadModel);
}

} // namespace

BatchVadTrimResult TrimBatchPcm16WithVad(const Config& config,
                                         IVadDetector& engine,
                                         const std::vector<BYTE>& pcm) {
    BatchVadTrimResult result;
    result.rawBytes = pcm.size();

    if (!config.enableVad) {
        result.error = L"VAD is disabled";
        return result;
    }
    if (pcm.size() < sizeof(int16_t)) {
        result.error = L"PCM is empty";
        return result;
    }

    const int threads = ResolveThreads(config.threads);
    bool ok = engine.EnsureVadForConfig(config, threads);
    if (ok) {
        engine.ResetVad(config.vadModel);
    }

    if (!ok) {
        result.error = L"VAD model is not available";
        return result;
    }

    result.active = true;
    result.modelName = (config.vadModel == L"firered") ? L"FireRed" : L"Silero";

    HiResTimer timer;
    VadTrimCore core;
    std::vector<std::vector<BYTE>> outputs;

    const size_t usableBytes = pcm.size() - (pcm.size() % sizeof(int16_t));
    for (size_t offset = 0; offset < usableBytes; offset += kFrameBytes) {
        const size_t take = (std::min)(kFrameBytes, usableBytes - offset);
        std::vector<BYTE> frameData(pcm.begin() + offset, pcm.begin() + offset + take);
        const bool hasVoice = DetectVoice(config, engine, frameData);
        if (hasVoice) {
            result.detectedSpeech = true;
        }
        outputs.clear();
        core.ProcessChunk(std::move(frameData), hasVoice, outputs);
        for (const auto& chunk : outputs) {
            result.pcm.insert(result.pcm.end(), chunk.begin(), chunk.end());
        }
    }

    core.Finish();
    VadTrimCoreStats stats = core.Stats();
    result.detectedSpeech = stats.detectedSpeech;
    result.outputBytes = stats.outputBytes;
    result.elapsedMs = timer.ElapsedMs();
    return result;
}
