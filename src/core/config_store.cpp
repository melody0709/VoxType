#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config_store.h"
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

std::wstring NormalizeDiagnosticAudioMode(std::wstring mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') return static_cast<wchar_t>(ch - L'A' + L'a');
        return ch;
    });
    if (mode != L"failures" && mode != L"all") return L"off";
    return mode;
}

std::string ExtractJsonString(const std::string& json, const std::string& key, const std::string& fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return fallback;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return fallback;
    std::string value;
    bool escape = false;
    for (++pos; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escape) {
            switch (c) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            value.push_back(c);
        }
    }
    return value;
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return fallback;
    const size_t valueStart = json.find_first_not_of(" \t\r\n", pos + 1);
    if (valueStart == std::string::npos) return fallback;
    if (json.compare(valueStart, 4, "true") == 0) return true;
    if (json.compare(valueStart, 5, "false") == 0) return false;
    if (json.compare(valueStart, 1, "1") == 0) return true;
    if (json.compare(valueStart, 1, "0") == 0) return false;
    return fallback;
}

int ExtractJsonInt(const std::string& json, const std::string& key, int fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return fallback;
    const size_t valueStart = json.find_first_not_of(" \t\r\n", pos + 1);
    if (valueStart == std::string::npos) return fallback;
    if (json[valueStart] == '"') {
        size_t end = json.find('"', valueStart + 1);
        if (end == std::string::npos) return fallback;
        try {
            return std::stoi(json.substr(valueStart + 1, end - valueStart - 1));
        } catch (...) {
            return fallback;
        }
    }
    size_t valueEnd = json.find_first_of(",}\r\n", valueStart);
    if (valueEnd == std::string::npos) valueEnd = json.size();
    try {
        return std::stoi(json.substr(valueStart, valueEnd - valueStart));
    } catch (...) {
        return fallback;
    }
}

float ExtractJsonFloat(const std::string& json, const std::string& key, float fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return fallback;
    const size_t valueStart = json.find_first_not_of(" \t\r\n", pos + 1);
    if (valueStart == std::string::npos) return fallback;
    if (json[valueStart] == '"') {
        size_t end = json.find('"', valueStart + 1);
        if (end == std::string::npos) return fallback;
        try {
            return std::stof(json.substr(valueStart + 1, end - valueStart - 1));
        } catch (...) {
            return fallback;
        }
    }
    size_t valueEnd = json.find_first_of(",}\r\n", valueStart);
    if (valueEnd == std::string::npos) valueEnd = json.size();
    try {
        return std::stof(json.substr(valueStart, valueEnd - valueStart));
    } catch (...) {
        return fallback;
    }
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
    config.modelId = Utf8ToWide(ExtractJsonString(json, "model_id", WideToUtf8(config.modelId)));
    config.modelDir = Utf8ToWide(ExtractJsonString(json, "model_dir", WideToUtf8(config.modelDir)));
    config.threads = Utf8ToWide(ExtractJsonString(json, "threads", WideToUtf8(config.threads)));
    config.enableVad = ExtractJsonBool(json, "enable_vad", config.enableVad);
    config.vadModel = Utf8ToWide(ExtractJsonString(json, "vad_model", WideToUtf8(config.vadModel)));
    config.vadThreshold = ExtractJsonFloat(json, "vad_threshold", config.vadThreshold);
    config.vadMinSilence = ExtractJsonInt(json, "vad_min_silence", config.vadMinSilence);
    config.vadMinSpeech = ExtractJsonInt(json, "vad_min_speech", config.vadMinSpeech);
    config.vadPadStart = ExtractJsonInt(json, "vad_pad_start", config.vadPadStart);
    config.vadSmoothWindow = ExtractJsonInt(json, "vad_smooth_window", config.vadSmoothWindow);
    config.enablePartial = ExtractJsonBool(json, "enable_partial", config.enablePartial);
    config.postprocess = Utf8ToWide(ExtractJsonString(json, "postprocess", WideToUtf8(config.postprocess)));
    config.hotkey = Utf8ToWide(ExtractJsonString(json, "hotkey", WideToUtf8(config.hotkey)));
    config.llmProvider = Utf8ToWide(ExtractJsonString(json, "llm_provider", ""));
    config.llmProvidersJson = Utf8ToWide(ExtractJsonString(json, "llm_providers_json", ""));
    config.llmEndpoint = Utf8ToWide(ExtractJsonString(json, "llm_endpoint", WideToUtf8(config.llmEndpoint)));
    config.llmApiKey = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "llm_api_key", "")));
    config.llmModel = Utf8ToWide(ExtractJsonString(json, "llm_model", WideToUtf8(config.llmModel)));
    config.llmPrompt = Utf8ToWide(ExtractJsonString(json, "llm_prompt", ""));
    config.enableLlmDebug = ExtractJsonBool(json, "enable_llm_debug", false);
    config.enableDebugMode = ExtractJsonBool(json, "enable_debug_mode", false);
    config.forceUnicodeInput = ExtractJsonBool(json, "force_unicode_input", false);
    config.asrBackend = Utf8ToWide(ExtractJsonString(json, "asr_backend", WideToUtf8(config.asrBackend)));
    config.fallbackAsrBackend = Utf8ToWide(ExtractJsonString(json, "fallback_asr_backend", "none"));
    if (config.fallbackAsrBackend.empty()) config.fallbackAsrBackend = L"none";
    config.baiduApiKey = Utf8ToWide(ExtractJsonString(json, "baidu_api_key", ""));
    config.baiduSecretKey = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "baidu_secret_key", "")));
    config.baiduDevPid = ExtractJsonInt(json, "baidu_dev_pid", 1537);
    config.cloudProvider = Utf8ToWide(ExtractJsonString(json, "cloud_provider", "volcengine"));
    if (config.cloudProvider.empty()) config.cloudProvider = L"volcengine";
    config.maiApiProvider = Utf8ToWide(
        ExtractJsonString(json, "mai_api_provider", "openrouter"));
    if (config.maiApiProvider != L"azure") {
        config.maiApiProvider = L"openrouter";
    }
    config.maiOpenRouterApiKey = llm::DecryptString(Utf8ToWide(
        ExtractJsonString(json, "mai_openrouter_api_key", "")));
    config.maiAzureEndpoint = Trim(Utf8ToWide(
        ExtractJsonString(json, "mai_azure_endpoint", "")));
    while (config.maiAzureEndpoint.size() > 8 &&
           config.maiAzureEndpoint.back() == L'/') {
        config.maiAzureEndpoint.pop_back();
    }
    config.maiAzureApiKey = llm::DecryptString(Utf8ToWide(
        ExtractJsonString(json, "mai_azure_api_key", "")));
    config.maiLanguage = Utf8ToWide(
        ExtractJsonString(json, "mai_language", "auto"));
    if (config.maiLanguage != L"zh" && config.maiLanguage != L"en" &&
        config.maiLanguage != L"yue") {
        config.maiLanguage = L"auto";
    }
    config.volcApiKey = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "volc_api_key", "")));
    config.volcResourceId = Utf8ToWide(ExtractJsonString(json, "volc_resource_id", "volc.seedasr.sauc.duration"));
    config.volcMode = Utf8ToWide(ExtractJsonString(json, "volc_mode", "bigmodel_nostream"));
    config.volcLanguage = Utf8ToWide(ExtractJsonString(json, "volc_language", ""));
    config.volcEnableNonstream = ExtractJsonBool(json, "volc_enable_nonstream", false);
    config.volcEndWindowSize = _wtoi(Utf8ToWide(ExtractJsonString(json, "volc_end_window_size", "800")).c_str());
    if (config.volcEndWindowSize <= 0) config.volcEndWindowSize = 800;
    config.volcEnableDdc = ExtractJsonBool(json, "volc_enable_ddc", false);
    config.volcExtraParams = Utf8ToWide(ExtractJsonString(json, "volc_extra_params", ""));
    config.volcEnableContext = ExtractJsonBool(json, "volc_enable_context", false);
    config.volcContextHistory = ExtractJsonInt(json, "volc_context_history", 3);
    if (config.volcContextHistory < 1) config.volcContextHistory = 3;
    if (config.volcContextHistory > 20) config.volcContextHistory = 20;
    config.volcEnableInputContext = ExtractJsonBool(json, "volc_enable_input_context", false);
    config.volcEnableMusicFc = ExtractJsonBool(json, "volc_enable_music_fc", false);
    config.volcEnablePoiFc = ExtractJsonBool(json, "volc_enable_poi_fc", false);
    config.volcForceToSpeechTime = ExtractJsonInt(json, "volc_force_to_speech_time", 0);
    config.volcHotwordsId = Utf8ToWide(ExtractJsonString(json, "volc_hotwords_id", ""));
    config.volcHotwordsName = Utf8ToWide(ExtractJsonString(json, "volc_hotwords_name", ""));
    config.volcCorrectTableId = Utf8ToWide(ExtractJsonString(json, "volc_correct_table_id", ""));
    config.volcCorrectTableName = Utf8ToWide(ExtractJsonString(json, "volc_correct_table_name", ""));
    const bool hasPersistedQwenModel = json.find("\"qwen_model\"") != std::string::npos;
    const bool hasPersistedQwenTransport = json.find("\"qwen_transport\"") != std::string::npos;
    config.qwenApiKey = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "qwen_api_key", "")));
    config.qwenBaseUrl = Utf8ToWide(ExtractJsonString(json, "qwen_base_url", WideToUtf8(config.qwenBaseUrl)));
    config.qwenModel = Utf8ToWide(ExtractJsonString(json, "qwen_model", WideToUtf8(config.qwenModel)));
    config.qwenTransport = Utf8ToWide(ExtractJsonString(json, "qwen_transport", WideToUtf8(config.qwenTransport)));
    config.qwenHttpBaseUrl = Utf8ToWide(ExtractJsonString(json, "qwen_http_base_url", WideToUtf8(config.qwenHttpBaseUrl)));
    config.qwenAudioStreamingBaseUrl = Utf8ToWide(ExtractJsonString(json, "qwen_audio_streaming_base_url", WideToUtf8(config.qwenAudioStreamingBaseUrl)));
    config.qwenLanguage = Utf8ToWide(ExtractJsonString(json, "qwen_language", ""));
    config.qwenChunkMs = ExtractJsonInt(json, "qwen_chunk_ms", config.qwenChunkMs);
    config.qwenLanguageHints = Utf8ToWide(ExtractJsonString(
        json, "qwen_language_hints", WideToUtf8(config.qwenLanguageHints)));
    config.qwenVocabularyId = Utf8ToWide(ExtractJsonString(json, "qwen_vocabulary_id", ""));
    config.qwenVocabulary = Utf8ToWide(ExtractJsonString(json, "qwen_vocabulary", ""));
    config.qwenSemanticPunctuation = ExtractJsonBool(json, "qwen_semantic_punctuation", config.qwenSemanticPunctuation);
    config.qwenMaxSentenceSilenceMs = std::clamp(ExtractJsonInt(json, "qwen_max_sentence_silence", config.qwenMaxSentenceSilenceMs), 200, 6000);
    config.qwenMultiThresholdMode = ExtractJsonBool(json, "qwen_multi_threshold", config.qwenMultiThresholdMode);
    config.qwenHeartbeat = ExtractJsonBool(json, "qwen_heartbeat", config.qwenHeartbeat);
    config.qwenSpeechNoiseThresholdEnabled = ExtractJsonBool(json, "qwen_speech_noise_threshold_enabled", config.qwenSpeechNoiseThresholdEnabled);
    config.qwenSpeechNoiseThreshold = std::clamp(ExtractJsonFloat(json, "qwen_speech_noise_threshold", config.qwenSpeechNoiseThreshold), -1.0f, 1.0f);
    config.qwenEnableInputContext = ExtractJsonBool(json, "qwen_enable_input_context", config.qwenEnableInputContext);
    config.qwenEnableContinueContext = ExtractJsonBool(json, "qwen_enable_continue_context", config.qwenEnableContinueContext);
    config.qwenEnableContinueContext =
        config.qwenEnableContinueContext && config.qwenEnableInputContext;
    config.qwenSpecialWordReplaceList = Utf8ToWide(ExtractJsonString(
        json, "qwen_special_word_replace", WideToUtf8(config.qwenSpecialWordReplaceList)));
    config.qwenSpecialWordEmptyList = Utf8ToWide(ExtractJsonString(
        json, "qwen_special_word_empty", WideToUtf8(config.qwenSpecialWordEmptyList)));
    config.qwenSystemReservedFilter = ExtractJsonBool(
        json, "qwen_system_reserved_filter", config.qwenSystemReservedFilter);
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
    config.mimoApiKey = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "mimo_api_key", "")));
    config.mimoBaseUrl = Utf8ToWide(ExtractJsonString(json, "mimo_base_url", WideToUtf8(config.mimoBaseUrl)));
    config.mimoModel = Utf8ToWide(ExtractJsonString(json, "mimo_model", WideToUtf8(config.mimoModel)));
    config.mimoLanguage = Utf8ToWide(ExtractJsonString(json, "mimo_language", WideToUtf8(config.mimoLanguage)));
    if (config.mimoBaseUrl.empty()) config.mimoBaseUrl = L"https://token-plan-ams.xiaomimimo.com/v1";
    if (config.mimoModel.empty()) config.mimoModel = L"mimo-v2.5-asr";
    if (config.mimoLanguage != L"zh" && config.mimoLanguage != L"en") config.mimoLanguage = L"auto";
    config.doubaoImeDeviceId = Utf8ToWide(ExtractJsonString(json, "doubao_ime_device_id", ""));
    config.doubaoImeCdid = Utf8ToWide(ExtractJsonString(json, "doubao_ime_cdid", ""));
    config.doubaoImeToken = llm::DecryptString(Utf8ToWide(ExtractJsonString(json, "doubao_ime_token", "")));
    config.qwenFreePolishEnabled = ExtractJsonBool(json, "qwen_free_polish", false);
    config.qwenFreePunctEnabled = ExtractJsonBool(json, "qwen_free_punct", false);
    config.qwenFreeCorrectEnabled = ExtractJsonBool(json, "qwen_free_correct", false);
    (void)ExtractJsonBool(json, "qwen_free_rewrite", false);
    config.qwenFreeRewriteEnabled = false;
    config.qwenFreeDebugLog = ExtractJsonBool(json, "qwen_free_debug_log", false);
    config.qwenFreeShellPath = Utf8ToWide(ExtractJsonString(json, "qwen_free_shell_path", ""));
    {
        const std::wstring storedUtdid = Utf8ToWide(
            ExtractJsonString(json, "qwen_free_utdid_override", ""));
        const std::wstring decryptedUtdid = llm::DecryptString(storedUtdid);
        config.qwenFreeUtdidOverride = decryptedUtdid.empty()
            ? storedUtdid : decryptedUtdid;
        migratePlaintextQwenUtdid =
            storedUtdid.size() == 24 && decryptedUtdid.empty() &&
            std::all_of(storedUtdid.begin(), storedUtdid.end(), [](wchar_t ch) {
                return (ch >= L'0' && ch <= L'9') ||
                       (ch >= L'A' && ch <= L'Z') ||
                       (ch >= L'a' && ch <= L'z');
            });
    }
    NormalizeQwenFreePostProcessConfig(config);
    config.audioBackend = Utf8ToWide(ExtractJsonString(json, "audio_backend", WideToUtf8(config.audioBackend)));
    config.audioDeviceId = Utf8ToWide(ExtractJsonString(json, "audio_device_id", ""));
    config.diagnosticAudioMode = NormalizeDiagnosticAudioMode(
        Utf8ToWide(ExtractJsonString(json, "diagnostic_audio_mode", "off")));
    config.configVersion = ExtractJsonInt(json, "config_version", 0);

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
    Config cfgCopy = config;
    SaveCurrentProvider(cfgCopy);
    NormalizeQwenFreePostProcessConfig(cfgCopy);
    cfgCopy.diagnosticAudioMode = NormalizeDiagnosticAudioMode(cfgCopy.diagnosticAudioMode);

    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    file << "{\n"
         << "  \"config_version\": " << cfgCopy.configVersion << ",\n"
         << "  \"model_id\": \"" << EscapeJson(cfgCopy.modelId) << "\",\n"
         << "  \"model_dir\": \"" << EscapeJson(cfgCopy.modelDir) << "\",\n"
         << "  \"threads\": \"" << EscapeJson(cfgCopy.threads) << "\",\n"
         << "  \"enable_vad\": " << (cfgCopy.enableVad ? "true" : "false") << ",\n"
         << "  \"vad_model\": \"" << EscapeJson(cfgCopy.vadModel) << "\",\n"
         << "  \"vad_threshold\": " << cfgCopy.vadThreshold << ",\n"
         << "  \"vad_min_silence\": " << cfgCopy.vadMinSilence << ",\n"
         << "  \"vad_min_speech\": " << cfgCopy.vadMinSpeech << ",\n"
         << "  \"vad_pad_start\": " << cfgCopy.vadPadStart << ",\n"
         << "  \"vad_smooth_window\": " << cfgCopy.vadSmoothWindow << ",\n"
         << "  \"enable_partial\": " << (cfgCopy.enablePartial ? "true" : "false") << ",\n"
         << "  \"postprocess\": \"" << EscapeJson(cfgCopy.postprocess) << "\",\n"
         << "  \"hotkey\": \"" << EscapeJson(cfgCopy.hotkey) << "\",\n"
         << "  \"llm_provider\": \"" << EscapeJson(cfgCopy.llmProvider) << "\",\n"
         << "  \"llm_providers_json\": \"" << EscapeJson(cfgCopy.llmProvidersJson) << "\",\n"
         << "  \"llm_endpoint\": \"" << EscapeJson(cfgCopy.llmEndpoint) << "\",\n"
         << "  \"llm_api_key\": \"" << EscapeJson(llm::EncryptString(cfgCopy.llmApiKey)) << "\",\n"
         << "  \"llm_model\": \"" << EscapeJson(cfgCopy.llmModel) << "\",\n"
         << "  \"llm_prompt\": \"" << EscapeJson(cfgCopy.llmPrompt) << "\",\n"
         << "  \"enable_llm_debug\": " << (cfgCopy.enableLlmDebug ? "true" : "false") << ",\n"
         << "  \"enable_debug_mode\": " << (cfgCopy.enableDebugMode ? "true" : "false") << ",\n"
         << "  \"force_unicode_input\": " << (cfgCopy.forceUnicodeInput ? "true" : "false") << ",\n"
         << "  \"asr_backend\": \"" << EscapeJson(cfgCopy.asrBackend) << "\",\n"
         << "  \"fallback_asr_backend\": \"" << EscapeJson(cfgCopy.fallbackAsrBackend) << "\",\n"
         << "  \"baidu_api_key\": \"" << EscapeJson(cfgCopy.baiduApiKey) << "\",\n"
         << "  \"baidu_secret_key\": \"" << EscapeJson(llm::EncryptString(cfgCopy.baiduSecretKey)) << "\",\n"
         << "  \"baidu_dev_pid\": " << cfgCopy.baiduDevPid << ",\n"
         << "  \"cloud_provider\": \"" << EscapeJson(cfgCopy.cloudProvider) << "\",\n"
         << "  \"mai_api_provider\": \"" << EscapeJson(cfgCopy.maiApiProvider) << "\",\n"
         << "  \"mai_openrouter_api_key\": \""
         << EscapeJson(llm::EncryptString(cfgCopy.maiOpenRouterApiKey)) << "\",\n"
         << "  \"mai_azure_endpoint\": \"" << EscapeJson(cfgCopy.maiAzureEndpoint) << "\",\n"
         << "  \"mai_azure_api_key\": \""
         << EscapeJson(llm::EncryptString(cfgCopy.maiAzureApiKey)) << "\",\n"
         << "  \"mai_language\": \"" << EscapeJson(cfgCopy.maiLanguage) << "\",\n"
         << "  \"volc_api_key\": \"" << EscapeJson(llm::EncryptString(cfgCopy.volcApiKey)) << "\",\n"
         << "  \"volc_resource_id\": \"" << EscapeJson(cfgCopy.volcResourceId) << "\",\n"
         << "  \"volc_mode\": \"" << EscapeJson(cfgCopy.volcMode) << "\",\n"
         << "  \"volc_language\": \"" << EscapeJson(cfgCopy.volcLanguage) << "\",\n"
         << "  \"volc_enable_nonstream\": " << (cfgCopy.volcEnableNonstream ? "1" : "0") << ",\n"
         << "  \"volc_end_window_size\": " << cfgCopy.volcEndWindowSize << ",\n"
         << "  \"volc_enable_ddc\": " << (cfgCopy.volcEnableDdc ? "1" : "0") << ",\n"
         << "  \"volc_extra_params\": \"" << EscapeJson(cfgCopy.volcExtraParams) << "\",\n"
         << "  \"volc_enable_context\": " << (cfgCopy.volcEnableContext ? "1" : "0") << ",\n"
         << "  \"volc_context_history\": " << cfgCopy.volcContextHistory << ",\n"
         << "  \"volc_enable_input_context\": " << (cfgCopy.volcEnableInputContext ? "1" : "0") << ",\n"
         << "  \"volc_enable_music_fc\": " << (cfgCopy.volcEnableMusicFc ? "1" : "0") << ",\n"
         << "  \"volc_enable_poi_fc\": " << (cfgCopy.volcEnablePoiFc ? "1" : "0") << ",\n"
         << "  \"volc_force_to_speech_time\": " << cfgCopy.volcForceToSpeechTime << ",\n"
         << "  \"volc_hotwords_id\": \"" << EscapeJson(cfgCopy.volcHotwordsId) << "\",\n"
         << "  \"volc_hotwords_name\": \"" << EscapeJson(cfgCopy.volcHotwordsName) << "\",\n"
         << "  \"volc_correct_table_id\": \"" << EscapeJson(cfgCopy.volcCorrectTableId) << "\",\n"
         << "  \"volc_correct_table_name\": \"" << EscapeJson(cfgCopy.volcCorrectTableName) << "\",\n"
         << "  \"qwen_api_key\": \"" << EscapeJson(llm::EncryptString(cfgCopy.qwenApiKey)) << "\",\n"
         << "  \"qwen_base_url\": \"" << EscapeJson(cfgCopy.qwenBaseUrl) << "\",\n"
         << "  \"qwen_http_base_url\": \"" << EscapeJson(cfgCopy.qwenHttpBaseUrl) << "\",\n"
         << "  \"qwen_audio_streaming_base_url\": \"" << EscapeJson(cfgCopy.qwenAudioStreamingBaseUrl) << "\",\n"
         << "  \"qwen_model\": \"" << EscapeJson(cfgCopy.qwenModel) << "\",\n"
         << "  \"qwen_transport\": \"" << EscapeJson(cfgCopy.qwenTransport) << "\",\n"
         << "  \"qwen_language\": \"" << EscapeJson(cfgCopy.qwenLanguage) << "\",\n"
         << "  \"qwen_chunk_ms\": " << cfgCopy.qwenChunkMs << ",\n"
         << "  \"qwen_language_hints\": \"" << EscapeJson(cfgCopy.qwenLanguageHints) << "\",\n"
         << "  \"qwen_vocabulary_id\": \"" << EscapeJson(cfgCopy.qwenVocabularyId) << "\",\n"
         << "  \"qwen_vocabulary\": \"" << EscapeJson(cfgCopy.qwenVocabulary) << "\",\n"
         << "  \"qwen_semantic_punctuation\": " << (cfgCopy.qwenSemanticPunctuation ? "true" : "false") << ",\n"
         << "  \"qwen_max_sentence_silence\": " << cfgCopy.qwenMaxSentenceSilenceMs << ",\n"
         << "  \"qwen_multi_threshold\": " << (cfgCopy.qwenMultiThresholdMode ? "true" : "false") << ",\n"
         << "  \"qwen_heartbeat\": " << (cfgCopy.qwenHeartbeat ? "true" : "false") << ",\n"
         << "  \"qwen_speech_noise_threshold_enabled\": " << (cfgCopy.qwenSpeechNoiseThresholdEnabled ? "true" : "false") << ",\n"
         << "  \"qwen_speech_noise_threshold\": " << cfgCopy.qwenSpeechNoiseThreshold << ",\n"
         << "  \"qwen_enable_input_context\": " << (cfgCopy.qwenEnableInputContext ? "true" : "false") << ",\n"
         << "  \"qwen_enable_continue_context\": " << (cfgCopy.qwenEnableContinueContext ? "true" : "false") << ",\n"
         << "  \"qwen_special_word_replace\": \"" << EscapeJson(cfgCopy.qwenSpecialWordReplaceList) << "\",\n"
         << "  \"qwen_special_word_empty\": \"" << EscapeJson(cfgCopy.qwenSpecialWordEmptyList) << "\",\n"
         << "  \"qwen_system_reserved_filter\": " << (cfgCopy.qwenSystemReservedFilter ? "true" : "false") << ",\n"
         << "  \"mimo_api_key\": \"" << EscapeJson(llm::EncryptString(cfgCopy.mimoApiKey)) << "\",\n"
         << "  \"mimo_base_url\": \"" << EscapeJson(cfgCopy.mimoBaseUrl) << "\",\n"
         << "  \"mimo_model\": \"" << EscapeJson(cfgCopy.mimoModel) << "\",\n"
         << "  \"mimo_language\": \"" << EscapeJson(cfgCopy.mimoLanguage) << "\",\n"
         << "  \"doubao_ime_device_id\": \"" << EscapeJson(cfgCopy.doubaoImeDeviceId) << "\",\n"
         << "  \"doubao_ime_cdid\": \"" << EscapeJson(cfgCopy.doubaoImeCdid) << "\",\n"
         << "  \"doubao_ime_token\": \"" << EscapeJson(llm::EncryptString(cfgCopy.doubaoImeToken)) << "\",\n"
         << "  \"qwen_free_polish\": " << (cfgCopy.qwenFreePolishEnabled ? 1 : 0) << ",\n"
         << "  \"qwen_free_punct\": " << (cfgCopy.qwenFreePunctEnabled ? 1 : 0) << ",\n"
         << "  \"qwen_free_correct\": " << (cfgCopy.qwenFreeCorrectEnabled ? 1 : 0) << ",\n"
         << "  \"qwen_free_rewrite\": " << (cfgCopy.qwenFreeRewriteEnabled ? 1 : 0) << ",\n"
         << "  \"qwen_free_debug_log\": " << (cfgCopy.qwenFreeDebugLog ? 1 : 0) << ",\n"
         << "  \"qwen_free_shell_path\": \"" << EscapeJson(cfgCopy.qwenFreeShellPath) << "\",\n"
         << "  \"qwen_free_utdid_override\": \""
         << EscapeJson(llm::EncryptString(cfgCopy.qwenFreeUtdidOverride)) << "\",\n"
         << "  \"audio_backend\": \"" << EscapeJson(cfgCopy.audioBackend) << "\",\n"
         << "  \"audio_device_id\": \"" << EscapeJson(cfgCopy.audioDeviceId) << "\",\n"
         << "  \"diagnostic_audio_mode\": \""
         << EscapeJson(cfgCopy.diagnosticAudioMode)
         << "\"\n"
         << "}\n";
}
