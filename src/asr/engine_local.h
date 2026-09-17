#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config_store.h"
#include "vad_detector.h"
#include "firered_vad.h"
#include "sherpa-onnx/c-api/cxx-api.h"

#include "utils.h"

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct VadResult {
    bool hasSpeech = false;
    std::vector<float> samples;
};

class AsrEngine : public IVadDetector {
public:
    void Lock() override { lock_.lock(); }
    void Unlock() override { lock_.unlock(); }

    std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> recognizer;
    std::unique_ptr<sherpa_onnx::cxx::VoiceActivityDetector> vad;
    std::unique_ptr<sherpa_onnx::cxx::OfflinePunctuation> punctuation;
    std::unique_ptr<firered_vad::FireRedVad> fireRedVad;
    std::string recognizerKey;
    std::string vadKey;
    std::string fireRedVadKey;
    std::string punctKey;

    std::string MakeKey(const std::wstring& modelId, const std::wstring& modelDir, int threads);
    bool EnsureRecognizer(const Config& config);
    bool EnsurePunctuation(int threads);
    VadResult ApplyVad(const std::vector<float>& samples, const Config& config, int threads);
    bool EnsureVadForConfig(const Config& config, int threads) override;
    void ResetVad(const std::wstring& vadModel) override;
    bool DetectSpeech(const float* samples, size_t count, const std::wstring& vadModel) override;
    std::wstring Recognize(const std::vector<float>& samples, int sampleRate, const Config& config);
    void Reload();

private:
    std::mutex lock_;
    bool EnsureVad(int threads, const Config& config);
    bool EnsureFireRedVad(const Config& config);
};

void PreloadAsrEngine(const Config& config);
std::vector<float> PcmToFloat(const std::vector<BYTE>& pcm);
AsrEngine& GetLocalAsrEngine();
extern AsrEngine g_asrEngine;

