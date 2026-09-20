#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "asr_probe_service_impl.h"
#include "asr_probe_service.h"

#include "baidu_asr.h"
#include "volcengine_asr.h"
#include "qwen_asr.h"
#include "qwen_audio_http.h"
#include "qwen_audio_streaming.h"
#include "mimo_asr.h"
#include "mai_transcribe.h"
#include "doubao_ime_asr.h"
#include "qwen_free_proto_utdid.h"
#include "qwen_free_proto_unet.h"
#include "qwen_free_proto_asr.h"
#include "qwen_free_proto_llm.h"
#include "qwen_audio_json.h"
#include "audio_diagnostics.h"

#include <thread>
#include <algorithm>

namespace asr_probe {

namespace {

class AsrProbeServiceImpl : public IAsrProbeService {
public:
    void ProbeAsync(const ProbeRequest& req, ProbeCallback callback) override {
        std::thread([req, callback = std::move(callback)]() {
            ProbeResult pr;

            if (req.provider == L"baidu") {
                baidu_asr::BaiduConfig bcfg;
                bcfg.apiKey = req.configSnapshot.baiduApiKey;
                bcfg.secretKey = req.configSnapshot.baiduSecretKey;
                bcfg.devPid = req.configSnapshot.baiduDevPid;
                auto res = baidu_asr::TestConnection(bcfg);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                callback(pr);
                return;
            }

            if (req.provider == L"volcengine") {
                volc_asr::VolcConfig vcfg;
                vcfg.apiKey = req.configSnapshot.volcApiKey;
                vcfg.mode = req.configSnapshot.volcMode;
                vcfg.resourceId = req.configSnapshot.volcResourceId;
                vcfg.language = req.configSnapshot.volcLanguage;
                vcfg.enableNonstream = req.configSnapshot.volcEnableNonstream;
                vcfg.enableDdc = req.configSnapshot.volcEnableDdc;
                vcfg.enableMusicFc = req.configSnapshot.volcEnableMusicFc;
                vcfg.enablePoiFc = req.configSnapshot.volcEnablePoiFc;
                vcfg.endWindowSize = req.configSnapshot.volcEndWindowSize > 0 ? req.configSnapshot.volcEndWindowSize : 800;
                vcfg.forceToSpeechTime = req.configSnapshot.volcForceToSpeechTime >= 1 ? req.configSnapshot.volcForceToSpeechTime : 0;
                vcfg.extraParams = req.configSnapshot.volcExtraParams;
                vcfg.hotwordsId = req.configSnapshot.volcHotwordsId;
                vcfg.hotwordsName = req.configSnapshot.volcHotwordsName;
                vcfg.correctTableId = req.configSnapshot.volcCorrectTableId;
                vcfg.correctTableName = req.configSnapshot.volcCorrectTableName;
                auto res = volc_asr::TestConnection(vcfg);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                callback(pr);
                return;
            }

            if (req.provider == L"qwen") {
                const std::wstring& model = req.configSnapshot.qwenModel;
                if (model == L"qwen-audio-3.0-asr-flash") {
                    qwen_audio_http::Config cfg;
                    cfg.apiKey = req.configSnapshot.qwenApiKey;
                    cfg.baseUrl = req.configSnapshot.qwenHttpBaseUrl;
                    cfg.model = model;
                    cfg.languageHints = req.configSnapshot.qwenLanguageHints;
                    cfg.vocabularyId = req.configSnapshot.qwenVocabularyId;
                    cfg.vocabulary = req.configSnapshot.qwenVocabulary;
                    auto res = qwen_audio_http::TestConnection(cfg);
                    pr.ok = res.ok;
                    pr.message = std::move(res.message);
                    callback(pr);
                    return;
                }
                if (model == L"qwen-audio-3.0-asr-flash-streaming") {
                    qwen_audio_streaming::Config cfg;
                    cfg.apiKey = req.configSnapshot.qwenApiKey;
                    cfg.baseUrl = req.configSnapshot.qwenAudioStreamingBaseUrl;
                    cfg.model = model;
                    cfg.languageHints = req.configSnapshot.qwenLanguageHints;
                    cfg.vocabularyId = req.configSnapshot.qwenVocabularyId;
                    cfg.vocabulary = req.configSnapshot.qwenVocabulary;
                    cfg.semanticPunctuation = req.configSnapshot.qwenSemanticPunctuation;
                    cfg.multiThresholdMode = req.configSnapshot.qwenMultiThresholdMode;
                    cfg.heartbeat = req.configSnapshot.qwenHeartbeat;
                    cfg.speechNoiseThresholdEnabled = req.configSnapshot.qwenSpeechNoiseThresholdEnabled;
                    cfg.speechNoiseThreshold = req.configSnapshot.qwenSpeechNoiseThreshold;
                    cfg.maxSentenceSilenceMs = req.configSnapshot.qwenMaxSentenceSilenceMs;
                    cfg.specialWordReplaceList = req.configSnapshot.qwenSpecialWordReplaceList;
                    cfg.specialWordEmptyList = req.configSnapshot.qwenSpecialWordEmptyList;
                    cfg.systemReservedFilter = req.configSnapshot.qwenSystemReservedFilter;
                    auto res = qwen_audio_streaming::TestConnection(cfg);
                    pr.ok = res.ok;
                    pr.message = std::move(res.message);
                    callback(pr);
                    return;
                }
                qwen_asr::QwenConfig qcfg;
                qcfg.apiKey = req.configSnapshot.qwenApiKey;
                qcfg.baseUrl = req.configSnapshot.qwenBaseUrl.empty() ? qwen_asr::kDefaultBaseUrl : req.configSnapshot.qwenBaseUrl;
                qcfg.model = model.empty() ? qwen_asr::kDefaultModel : model;
                qcfg.language = req.configSnapshot.qwenLanguage;
                qcfg.turnDetection = L"manual";
                qcfg.chunkMs = req.configSnapshot.qwenChunkMs;
                auto res = qwen_asr::TestConnection(qcfg);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                callback(pr);
                return;
            }

            if (req.provider == L"mimo") {
                mimo_asr::MimoConfig mcfg;
                mcfg.apiKey = req.configSnapshot.mimoApiKey;
                mcfg.baseUrl = req.configSnapshot.mimoBaseUrl.empty() ? mimo_asr::kDefaultBaseUrl : req.configSnapshot.mimoBaseUrl;
                mcfg.model = req.configSnapshot.mimoModel.empty() ? mimo_asr::kDefaultModel : req.configSnapshot.mimoModel;
                mcfg.language = req.configSnapshot.mimoLanguage;
                auto res = mimo_asr::TestConnection(mcfg);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                callback(pr);
                return;
            }

            if (req.provider == L"mai") {
                mai_transcribe::Config config;
                config.apiProvider = (req.configSnapshot.maiApiProvider == L"azure")
                    ? mai_transcribe::ApiProvider::AzureSpeech
                    : mai_transcribe::ApiProvider::OpenRouter;
                config.apiKey = (req.configSnapshot.maiApiProvider == L"azure")
                    ? req.configSnapshot.maiAzureApiKey
                    : req.configSnapshot.maiOpenRouterApiKey;
                config.azureEndpoint = req.configSnapshot.maiAzureEndpoint;
                config.language = req.configSnapshot.maiLanguage;
                auto res = mai_transcribe::TestConnection(config);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                callback(pr);
                return;
            }

            if (req.provider == L"doubao_ime") {
                doubao_ime_asr::DoubaoImeConfig dcfg;
                dcfg.deviceId = req.configSnapshot.doubaoImeDeviceId;
                dcfg.cdid = req.configSnapshot.doubaoImeCdid;
                dcfg.token = req.configSnapshot.doubaoImeToken;
                auto res = doubao_ime_asr::TestConnection(dcfg);
                pr.ok = res.ok;
                pr.message = std::move(res.message);
                pr.credentialsChanged = res.credentialsChanged;
                pr.credentials.deviceId = res.credentials.deviceId;
                pr.credentials.cdid = res.credentials.cdid;
                pr.credentials.token = res.credentials.token;
                callback(pr);
                return;
            }

            if (req.provider == L"qwen_free") {
                const std::wstring shellPath = req.configSnapshot.qwenFreeShellPath;
                const std::wstring utdidOverride = req.configSnapshot.qwenFreeUtdidOverride;
                const bool llmRequired = req.configSnapshot.qwenFreePolishEnabled || req.configSnapshot.qwenFreeRewriteEnabled;
                auto utdid = qwen_free_proto_utdid::GetUtdid(utdidOverride, shellPath);
                if (!utdid.ok) {
                    pr.ok = false;
                    pr.message = L"UTDID acquisition failed: " + utdid.error;
                    callback(pr);
                    return;
                }
                std::wstring unetError;
                if (!qwen_free_proto_unet::Initialize(shellPath, unetError)) {
                    pr.ok = false;
                    pr.message = L"Native signer initialization failed: " + unetError;
                    callback(pr);
                    return;
                }
                qwen_free_proto_asr::AsrConfig asrCfg;
                asrCfg.utdid = utdid.utdid;
                auto asrRes = qwen_free_proto_asr::TestConnection(asrCfg);
                if (!asrRes.ok) {
                    pr.ok = false;
                    pr.asrOk = false;
                    pr.llmOk = false;
                    pr.message = std::move(asrRes.message);
                    callback(pr);
                    return;
                }
                pr.asrOk = true;
                if (llmRequired) {
                    qwen_free_proto_llm::LlmConfig llmCfg;
                    llmCfg.utdid = utdid.utdid;
                    llmCfg.shellPath = shellPath;
                    const auto llm = qwen_free_proto_llm::PolishText(llmCfg, L"连接测试", 0.0, 0);
                    if (!llm.ok) {
                        pr.ok = false;
                        pr.llmOk = false;
                        pr.message = L"UTDID OK (" + utdid.source + L"); " + asrRes.message + L" (" +
                                     std::to_wstring(asrRes.elapsedMs) + L"ms); LLM unavailable: " +
                                     (llm.error.empty() ? L"unknown error" : llm.error);
                    } else {
                        pr.ok = true;
                        pr.llmOk = true;
                        pr.message = L"UTDID OK (" + utdid.source + L"); " + asrRes.message + L" (" +
                                     std::to_wstring(asrRes.elapsedMs) + L"ms); LLM OK (" +
                                     std::to_wstring(llm.elapsedMs) + L"ms)";
                    }
                } else {
                    pr.ok = true;
                    pr.llmOk = true;
                    pr.message = L"UTDID OK (" + utdid.source + L"); " + asrRes.message + L" (" +
                                 std::to_wstring(asrRes.elapsedMs) + L"ms); LLM skipped (post-processing disabled)";
                }
                callback(pr);
                return;
            }

            pr.ok = false;
            pr.message = L"Unknown provider requested for probe: " + req.provider;
            callback(pr);
        }).detach();
    }

    bool ValidateVocabulary(const std::wstring& value, std::wstring* error = nullptr) override {
        return qwen_audio_json::IsValidVocabulary(value, error);
    }

    std::wstring GetQwenFreeStatus(const std::wstring& utdidOverride, const std::wstring& shellPath) override {
        const auto maskUtdid = [](const std::string& utdid) {
            if (utdid.size() <= 8) return std::wstring(L"(redacted)");
            return std::wstring(utdid.begin(), utdid.begin() + 4) + L"..." +
                   std::wstring(utdid.end() - 4, utdid.end());
        };
        auto r = qwen_free_proto_utdid::GetUtdid(utdidOverride, shellPath);
        if (!r.ok) {
            return L"UTDID unavailable: " + r.error;
        }
        return L"UTDID OK (" + r.source + L"): " + maskUtdid(r.utdid);
    }

    void InvalidateStreamingConnections() override {
        qwen_audio_streaming::InvalidateReusableConnections();
    }
};

} // namespace

void InitializeAsrProbeService() {
    static AsrProbeServiceImpl s_impl;
    RegisterProbeService(&s_impl);
}

std::wstring NormalizeDiagnosticAudioMode(const std::wstring& mode) {
    return audio_diagnostics::NormalizeMode(mode);
}

std::wstring GetDiagnosticAudioDir() {
    return audio_diagnostics::DiagnosticAudioDir();
}

bool EnsureDiagnosticAudioDir(std::wstring* error) {
    return audio_diagnostics::EnsureDiagnosticAudioDir(error);
}

bool DeleteDiagnosticManagedRecordings(size_t* deletedGroups, std::wstring* error) {
    return audio_diagnostics::DeleteManagedRecordings(deletedGroups, error);
}

} // namespace asr_probe
