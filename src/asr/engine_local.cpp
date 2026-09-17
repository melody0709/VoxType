#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "engine_local.h"
#include "path_service.h"
#include "utils.h"
#include "asr_metrics.h"

#include <algorithm>
#include <vector>

static bool TryLoadAsrDlls() {
    __try {
        HMODULE h = LoadLibraryW(L"sherpa-onnx-cxx-api.dll");
        if (h) { FreeLibrary(h); return true; }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string AsrEngine::MakeKey(const std::wstring& modelId, const std::wstring& modelDir, int threads) {
    return WideToUtf8(modelId) + "|" + WideToUtf8(modelDir) + "|" + std::to_string(threads);
}

bool AsrEngine::EnsureRecognizer(const Config& config) {
    if (!TryLoadAsrDlls()) return false;
    const std::wstring modelDir = config.modelDir.empty() ? DefaultModelDir(config.modelId) : config.modelDir;
    const int threads = ResolveThreads(config.threads);
    const std::string key = MakeKey(config.modelId, modelDir, threads);
    if (recognizer && recognizerKey == key) return true;

    sherpa_onnx::cxx::OfflineRecognizerConfig rc;
    rc.model_config.tokens = WideToUtf8(modelDir) + "\\tokens.txt";
    rc.model_config.num_threads = threads;
    rc.model_config.debug = false;

    if (config.modelId == L"firered_ctc") {
        rc.model_config.fire_red_asr_ctc.model = WideToUtf8(modelDir) + "\\model.int8.onnx";
    } else if (config.modelId == L"firered_aed") {
        rc.model_config.fire_red_asr.encoder = WideToUtf8(modelDir) + "\\encoder.int8.onnx";
        rc.model_config.fire_red_asr.decoder = WideToUtf8(modelDir) + "\\decoder.int8.onnx";
    } else if (config.modelId == L"sensevoice") {
        rc.model_config.sense_voice.model = WideToUtf8(modelDir) + "\\model.int8.onnx";
        rc.model_config.sense_voice.use_itn = true;
    }

    auto r = sherpa_onnx::cxx::OfflineRecognizer::Create(rc);
    if (!r.Get()) return false;
    recognizer = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(std::move(r));
    recognizerKey = key;
    return true;
}

bool AsrEngine::EnsureVad(int threads, const Config& config) {
    if (!TryLoadAsrDlls()) return false;
    const std::string key = "vad|" + std::to_string(threads) + "|" + std::to_string(config.vadThreshold) + "|" + std::to_string(config.vadMinSilence) + "|" + std::to_string(config.vadMinSpeech);
    if (vad && vadKey == key) return true;

    const std::wstring vadPath = RuntimeAssetDir() + L"\\models\\silero_vad.int8.onnx";
    if (GetFileAttributesW(vadPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    sherpa_onnx::cxx::VadModelConfig vc;
    vc.silero_vad.model = WideToUtf8(vadPath);
    vc.silero_vad.threshold = config.vadThreshold;
    vc.silero_vad.min_silence_duration = static_cast<float>(config.vadMinSilence) / 1000.0f;
    vc.silero_vad.min_speech_duration = static_cast<float>(config.vadMinSpeech) / 1000.0f;
    vc.silero_vad.max_speech_duration = 20.0f;
    vc.silero_vad.window_size = 512;
    vc.sample_rate = 16000;
    vc.num_threads = threads;

    auto v = sherpa_onnx::cxx::VoiceActivityDetector::Create(vc, 600.0f);
    if (!v.Get()) return false;
    vad = std::make_unique<sherpa_onnx::cxx::VoiceActivityDetector>(std::move(v));
    vadKey = key;
    return true;
}

bool AsrEngine::EnsureFireRedVad(const Config& config) {
    if (!TryLoadAsrDlls()) return false;
    const std::string key = "firered_vad|" + std::to_string(config.vadThreshold) + "|" + std::to_string(config.vadMinSilence) + "|" + std::to_string(config.vadMinSpeech) + "|" + std::to_string(config.vadPadStart) + "|" + std::to_string(config.vadSmoothWindow);
    if (fireRedVad && fireRedVadKey == key) return true;

    firered_vad::FireRedVadConfig cfg;
    cfg.modelPath = WideToUtf8(RuntimeAssetDir() + L"\\models\\fireredvad_stream_vad_with_cache.onnx");
    cfg.threshold = config.vadThreshold;
    cfg.minSilenceMs = config.vadMinSilence;
    cfg.minSpeechMs = config.vadMinSpeech;
    cfg.padStartMs = config.vadPadStart;
    cfg.smoothWindowSize = config.vadSmoothWindow;
    if (GetFileAttributesW(Utf8ToWide(cfg.modelPath).c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    auto v = firered_vad::FireRedVad::Create(cfg);
    if (!v) return false;
    fireRedVad = std::move(v);
    fireRedVadKey = key;
    return true;
}

bool AsrEngine::EnsurePunctuation(int threads) {
    if (!TryLoadAsrDlls()) return false;
    const std::string key = "punct|" + std::to_string(threads);
    if (punctuation && punctKey == key) return true;

    const std::wstring punctPath = DownloadedModelRoot() +
        L"\\sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8\\model.int8.onnx";
    if (GetFileAttributesW(punctPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    sherpa_onnx::cxx::OfflinePunctuationConfig pc;
    pc.model.ct_transformer = WideToUtf8(punctPath);
    pc.model.num_threads = threads;

    auto p = sherpa_onnx::cxx::OfflinePunctuation::Create(pc);
    if (!p.Get()) return false;
    punctuation = std::make_unique<sherpa_onnx::cxx::OfflinePunctuation>(std::move(p));
    punctKey = key;
    return true;
}

VadResult AsrEngine::ApplyVad(const std::vector<float>& samples, const Config& config, int threads) {
    VadResult result;
    if (config.vadModel == L"firered") {
        if (!EnsureFireRedVad(config)) return result;
        fireRedVad->Reset();
        int nSamples = static_cast<int>(samples.size());
        fireRedVad->Process(samples.data(), nSamples);
        fireRedVad->Flush();
        auto concat = fireRedVad->GetConcatenatedSamples(samples.data(), nSamples);
        if (!concat.empty()) {
            result.hasSpeech = true;
            result.samples = std::move(concat);
        }
    } else {
        if (!EnsureVad(threads, config)) return result;
        vad->Reset();
        const size_t windowSize = 512;
        for (size_t i = 0; i < samples.size(); i += windowSize) {
            size_t end = std::min(i + windowSize, samples.size());
            vad->AcceptWaveform(samples.data() + i, static_cast<int32_t>(end - i));
        }
        vad->Flush();
        while (!vad->IsEmpty()) {
            auto seg = vad->Front();
            result.samples.insert(result.samples.end(),
                                   seg.samples.begin(), seg.samples.end());
            result.hasSpeech = true;
            vad->Pop();
        }
    }
    return result;
}

bool AsrEngine::EnsureVadForConfig(const Config& config, int threads) {
    if (config.vadModel == L"firered") return EnsureFireRedVad(config);
    return EnsureVad(threads, config);
}

void AsrEngine::ResetVad(const std::wstring& vadModel) {
    std::lock_guard<std::mutex> lk(lock_);
    if (vadModel == L"firered") {
        if (fireRedVad) fireRedVad->Reset();
    } else {
        if (vad) vad->Reset();
    }
}

bool AsrEngine::DetectSpeech(const float* samples, size_t count, const std::wstring& vadModel) {
    if (!samples || count == 0) return false;
    std::lock_guard<std::mutex> lk(lock_);
    if (vadModel == L"firered") {
        if (!fireRedVad) return false;
        fireRedVad->Process(samples, static_cast<int>(count));
        return fireRedVad->IsInSpeech();
    } else {
        if (!vad) return false;
        vad->AcceptWaveform(samples, static_cast<int32_t>(count));
        return vad->IsDetected();
    }
}

std::wstring AsrEngine::Recognize(const std::vector<float>& samples, int sampleRate, const Config& config) {
    const int threads = ResolveThreads(config.threads);

    {
        std::lock_guard<std::mutex> g(lock_);
        if (!EnsureRecognizer(config)) return L"ASR failed: model load error";
    }

    std::vector<float> workSamples = samples;

    if (config.enableVad && workSamples.size() > 0) {
        HiResTimer tVad;
        std::lock_guard<std::mutex> g(lock_);
        VadResult vr = ApplyVad(workSamples, config, threads);
        double ms = tVad.ElapsedMs();
        if (config.enableDebugMode) {
            asr_metrics::SetVadMs(ms);
            asr_metrics::SetVadModelName((config.vadModel == L"firered") ? L"FireRed" : L"Silero");
        }
        if (!vr.hasSpeech) return L"";
        if (!vr.samples.empty()) workSamples = std::move(vr.samples);
        if (config.enableDebugMode) asr_metrics::SetVadTrimmedSamples(workSamples.size());
    }

    if (workSamples.empty()) return L"";

    std::wstring text;
    {
        HiResTimer tAsr;
        std::lock_guard<std::mutex> g(lock_);
        auto stream = recognizer->CreateStream();
        stream.AcceptWaveform(sampleRate, workSamples.data(), static_cast<int32_t>(workSamples.size()));
        recognizer->Decode(&stream);
        auto result = recognizer->GetResult(&stream);
        text = Utf8ToWide(result.text);
        double ms = tAsr.ElapsedMs();
        if (config.enableDebugMode) asr_metrics::SetAsrDecodeMs(ms);
    }

    if (text == L"<sil>" || text == L"<blk>") return L"";

    if (config.postprocess == L"itn" || config.postprocess == L"punct" || config.postprocess == L"llm") {
        HiResTimer tPunct;
        std::lock_guard<std::mutex> g(lock_);
        if (EnsurePunctuation(threads)) {
            std::string utf8 = WideToUtf8(text);
            std::string punctuated = punctuation->AddPunctuation(utf8);
            text = Utf8ToWide(punctuated);
        }
        double ms = tPunct.ElapsedMs();
        if (config.enableDebugMode) asr_metrics::SetPunctMs(ms);
    }

    return text;
}

void AsrEngine::Reload() {
    std::lock_guard<std::mutex> g(lock_);
    recognizer.reset();
    vad.reset();
    punctuation.reset();
    fireRedVad.reset();
    recognizerKey.clear();
    vadKey.clear();
    fireRedVadKey.clear();
    punctKey.clear();
}

AsrEngine g_asrEngine;

AsrEngine& GetLocalAsrEngine() {
    return g_asrEngine;
}

void PreloadAsrEngine(const Config& config) {
    const int threads = ResolveThreads(config.threads);
    auto& engine = GetLocalAsrEngine();
    engine.Lock();
    engine.EnsureRecognizer(config);
    if (config.enableVad) engine.EnsureVadForConfig(config, threads);
    if (config.postprocess == L"itn" || config.postprocess == L"punct" || config.postprocess == L"llm") {
        engine.EnsurePunctuation(threads);
    }
    engine.Unlock();
}
