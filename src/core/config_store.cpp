#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config_store.h"
#include "config_registry.h"
#include "path_service.h"
#include "utils.h"
#include "llm_refine.h"
#include "qwen_audio_profile.h"
#include "qwen_special_word_filter.h"
#include "debug_logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

static constexpr int kCurrentConfigVersion = 15;

Config g_config;
Config& GetGlobalConfig() { return g_config; }

std::wstring NormalizeDiagnosticAudioMode(std::wstring mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') return static_cast<wchar_t>(ch - L'A' + L'a');
        return ch;
    });
    if (mode != L"failures" && mode != L"all") return L"off";
    return mode;
}



void SaveCurrentProvider(Config& config) {
    const std::wstring& name = config.llmProvider;
    if (name.empty()) return;
    std::string key = llm::WideToUtf8(name);
    std::string json = llm::WideToUtf8(config.llmProvidersJson);
    if (json.empty()) json = "{}";
    std::string encKey = llm::WideToUtf8(llm::EncryptString(config.llmApiKey));
    std::string entry = "{"
        "\"endpoint\":\"" + EscapeJson(config.llmEndpoint) + "\","
        "\"api_key\":\"" + encKey + "\","
        "\"model\":\"" + EscapeJson(config.llmModel) + "\","
        "\"extra_params\":\"" + EscapeJson(config.llmExtraParams) + "\"}";
    if (!llm::SetJsonObjectMemberRaw(json, key, entry)) {
        debug_log::Write(
            "event=llm_provider_store_update_skipped reason=invalid_store provider_chars=%zu store_bytes=%zu",
            name.size(), json.size());
        return;
    }
    config.llmProvidersJson = llm::Utf8ToWide(json);
}

bool LoadProviderFromStore(Config& config, const std::wstring& name) {
    std::string json = llm::WideToUtf8(config.llmProvidersJson);
    std::string key = llm::WideToUtf8(name);
    std::string section;
    if (!llm::GetJsonObjectMemberRaw(json, key, section)) return false;
    std::vector<llm::JsonObjectMemberSpan> fields;
    if (!llm::ParseJsonObjectMembers(section, 0, fields)) return false;
    std::string value;
    if (llm::GetJsonObjectMemberString(section, "endpoint", value)) {
        config.llmEndpoint = Utf8ToWide(value);
    }
    if (llm::GetJsonObjectMemberString(section, "api_key", value)) {
        config.llmApiKey = llm::DecryptString(Utf8ToWide(value));
    } else {
        config.llmApiKey.clear();
    }
    if (llm::GetJsonObjectMemberString(section, "model", value)) {
        config.llmModel = Utf8ToWide(value);
    }
    if (llm::GetJsonObjectMemberString(section, "extra_params", value)) {
        config.llmExtraParams = Utf8ToWide(value);
    }
    return true;
}

int FindPresetIndex(const std::wstring& name) {
    for (int i = 0; i < llm::kProviderPresetCount; ++i) {
        if (name == llm::kProviderPresets[i].name) return i;
    }
    return -1;
}

bool ApplyPreset(Config& config, int index, bool preserveLegacyFields) {
    if (index < 0 || index >= llm::kProviderPresetCount) return false;
    const std::wstring legacyEndpoint = config.llmEndpoint;
    const std::wstring legacyApiKey = config.llmApiKey;
    const std::wstring legacyModel = config.llmModel;
    const std::wstring legacyExtraParams = config.llmExtraParams;
    const auto& p = llm::kProviderPresets[index];
    config.llmProvider = p.name;
    config.llmEndpoint = p.url;
    config.llmApiKey.clear();
    config.llmModel = p.defaultModel;
    config.llmExtraParams = p.extraParams;
    const bool loaded = LoadProviderFromStore(config, p.name);
    if (!loaded && preserveLegacyFields) {
        if (!legacyEndpoint.empty()) config.llmEndpoint = legacyEndpoint;
        config.llmApiKey = legacyApiKey;
        if (!legacyModel.empty()) config.llmModel = legacyModel;
        if (!legacyExtraParams.empty()) config.llmExtraParams = legacyExtraParams;
    }
    return llm::MigrateLegacyProviderConfig(
        config.llmProvider, config.llmEndpoint,
        config.llmModel, config.llmExtraParams);
}

static void EnsureRegistryInitialized() {
    static bool s_registryInitialized = false;
    if (!s_registryInitialized) {
        config_registry::InitializeRegistry();
        config_registry::Registry::Instance().SetPreSaveHook([](Config& cfg) {
            SaveCurrentProvider(cfg);
            NormalizeQwenFreePostProcessConfig(cfg);
            cfg.diagnosticAudioMode = NormalizeDiagnosticAudioMode(cfg.diagnosticAudioMode);
        });
        s_registryInitialized = true;
    }
}

void LoadConfig(Config& config) {
    MigrateLegacyConfigIfNeeded();
    std::ifstream file(ConfigPath(), std::ios::binary);
    if (!file) {
        config.configVersion = kCurrentConfigVersion;
        ApplyPreset(config, 0);
        return;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();
    bool migratePlaintextQwenUtdid = false;
    bool migrateLlmProvider = false;

    EnsureRegistryInitialized();
    config_registry::Registry::Instance().LoadJson(config, json);

    if (config.hotkey.empty() || config.hotkey == L"0xE5" || config.hotkey == L"0xe5") {
        config.hotkey = L"CapsLock";
    }
    if (config.fallbackAsrBackend.empty()) config.fallbackAsrBackend = L"none";
    if (config.cloudProvider.empty()) config.cloudProvider = L"volcengine";
    if (config.maiApiProvider != L"azure") {
        config.maiApiProvider = L"openrouter";
    }
    config.maiAzureEndpoint = Trim(config.maiAzureEndpoint);
    while (config.maiAzureEndpoint.size() > 8 &&
           config.maiAzureEndpoint.back() == L'/') {
        config.maiAzureEndpoint.pop_back();
    }
    if (config.maiLanguage != L"zh" && config.maiLanguage != L"en" &&
        config.maiLanguage != L"yue") {
        config.maiLanguage = L"auto";
    }
    if (config.volcEndWindowSize <= 0) config.volcEndWindowSize = 800;
    if (config.volcContextHistory < 1) config.volcContextHistory = 3;
    if (config.volcContextHistory > 20) config.volcContextHistory = 20;

    const bool hasPersistedQwenModel = json.find("\"qwen_model\"") != std::string::npos;
    const bool hasPersistedQwenTransport = json.find("\"qwen_transport\"") != std::string::npos;
    config.qwenMaxSentenceSilenceMs = std::clamp(config.qwenMaxSentenceSilenceMs, 200, 6000);
    config.qwenSpeechNoiseThreshold = std::clamp(config.qwenSpeechNoiseThreshold, -1.0f, 1.0f);
    config.qwenEnableContinueContext =
        config.qwenEnableContinueContext && config.qwenEnableInputContext;

    {
        qwen_special_word_filter::Config normalized;
        std::wstring filterError;
        if (qwen_special_word_filter::Normalize(
                config.qwenSpecialWordReplaceList,
                config.qwenSpecialWordEmptyList,
                config.qwenSystemReservedFilter,
                normalized, &filterError)) {
            config.qwenSpecialWordReplaceList =
                qwen_special_word_filter::JoinLines(normalized.replaceWords);
            config.qwenSpecialWordEmptyList =
                qwen_special_word_filter::JoinLines(normalized.emptyWords);
        } else {
            config.qwenSpecialWordReplaceList.clear();
            config.qwenSpecialWordEmptyList.clear();
            config.qwenSystemReservedFilter = false;
        }
    }

    constexpr wchar_t kOldQwenRealtimeBaseUrl[] =
        L"wss://dashscope.aliyuncs.com/api-ws/v1/realtime";
    constexpr wchar_t kOldQwenHttpBaseUrl[] =
        L"https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation";
    constexpr wchar_t kOldQwenAudioStreamingBaseUrl[] =
        L"wss://dashscope.aliyuncs.com/api-ws/v1/inference";
    if (config.qwenBaseUrl.empty() || config.qwenBaseUrl == kOldQwenRealtimeBaseUrl) {
        config.qwenBaseUrl = kQwenBeijingRealtimeBaseUrl;
    }
    if (config.qwenHttpBaseUrl.empty()) config.qwenHttpBaseUrl = kQwenBeijingHttpBaseUrl;
    if (config.qwenHttpBaseUrl == kOldQwenHttpBaseUrl) config.qwenHttpBaseUrl = kQwenBeijingHttpBaseUrl;
    if (config.qwenAudioStreamingBaseUrl.empty()) config.qwenAudioStreamingBaseUrl = kQwenBeijingAudioStreamingBaseUrl;
    if (config.qwenAudioStreamingBaseUrl == kOldQwenAudioStreamingBaseUrl) {
        config.qwenAudioStreamingBaseUrl = kQwenBeijingAudioStreamingBaseUrl;
    }
    qwen_audio_profile::NormalizePersistedProfile(
        config.qwenModel,
        config.qwenTransport,
        hasPersistedQwenModel,
        hasPersistedQwenTransport);
    config.qwenChunkMs = std::clamp(config.qwenChunkMs, 20, 1000);
    if (config.mimoBaseUrl.empty()) config.mimoBaseUrl = L"https://token-plan-ams.xiaomimimo.com/v1";
    if (config.mimoModel.empty()) config.mimoModel = L"mimo-v2.5-asr";
    if (config.mimoLanguage != L"zh" && config.mimoLanguage != L"en") config.mimoLanguage = L"auto";
    config.qwenFreeRewriteEnabled = false;
    {
        const std::wstring storedUtdid = Utf8ToWide(
            ExtractJsonString(json, "qwen_free_utdid_override", ""));
        const std::wstring decryptedUtdid = llm::DecryptString(storedUtdid);
        migratePlaintextQwenUtdid =
            storedUtdid.size() == 24 && decryptedUtdid.empty() &&
            std::all_of(storedUtdid.begin(), storedUtdid.end(), [](wchar_t ch) {
                return (ch >= L'0' && ch <= L'9') ||
                       (ch >= L'A' && ch <= L'Z') ||
                       (ch >= L'a' && ch <= L'z');
            });
    }
    NormalizeQwenFreePostProcessConfig(config);
    config.diagnosticAudioMode = NormalizeDiagnosticAudioMode(config.diagnosticAudioMode);

    if (config.configVersion < 1) {
        if (config.volcMode.empty()) config.volcMode = L"bigmodel_nostream";
        if (config.volcResourceId.empty()) config.volcResourceId = L"volc.seedasr.sauc.duration";
        if (config.cloudProvider.empty()) config.cloudProvider = L"volcengine";
    }
    if (config.configVersion < 2) {
        config.vadThreshold = 0.15f;
        config.vadMinSilence = 500;
        config.vadMinSpeech = 30;
        config.vadPadStart = 150;
    }
    if (config.configVersion < 9 &&
        ShouldFallbackFromLegacyModelDir(config.modelDir)) {
        config.modelDir = DefaultModelDir(config.modelId);
    }
    if (config.configVersion < 12 && config.qwenLanguageHints.empty()) {
        config.qwenLanguageHints = kQwenDefaultLanguageHints;
    }
    if (config.modelDir.empty()) {
        config.modelDir = DefaultModelDir(config.modelId);
    }
    if (config.llmProvider.empty()) {
        if (!config.llmEndpoint.empty()) {
            config.llmProvider = L"Custom";
            SaveCurrentProvider(config);
        } else {
            ApplyPreset(config, 0);
        }
    } else {
        int pi = FindPresetIndex(config.llmProvider);
        if (pi >= 0) {
            migrateLlmProvider = ApplyPreset(config, pi, true);
        } else {
            LoadProviderFromStore(config, config.llmProvider);
        }
    }

    if (config.configVersion < kCurrentConfigVersion ||
        migrateLlmProvider ||
        migratePlaintextQwenUtdid) {
        config.configVersion = kCurrentConfigVersion;
        SaveConfig(config);
    }
}

void SaveConfig(const Config& config) {
    EnsureRegistryInitialized();
    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file << config_registry::Registry::Instance().SaveJson(config);
    }
}
