#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <cstdint>

#ifndef AUDIO_DIAGNOSTICS_STAGE_KIND_DEFINED
#define AUDIO_DIAGNOSTICS_STAGE_KIND_DEFINED
namespace audio_diagnostics {
enum class StageKind {
    Primary,
    InternalRetry,
    Fallback,
};
}
#endif

#ifndef VOXTYPE_CONFIG_CONSTANTS_DEFINED
#define VOXTYPE_CONFIG_CONSTANTS_DEFINED
constexpr wchar_t kQwenBeijingHttpBaseUrl[] =
    L"https://llm-c6rtn7zy4nw0u39k.cn-beijing.maas.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation";
constexpr wchar_t kQwenBeijingAudioStreamingBaseUrl[] =
    L"wss://llm-c6rtn7zy4nw0u39k.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference";
constexpr wchar_t kQwenBeijingRealtimeBaseUrl[] =
    L"wss://llm-c6rtn7zy4nw0u39k.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime";
constexpr wchar_t kQwenDefaultLanguageHints[] = L"zh,en,yue";
#endif

#ifndef VOXTYPE_CONFIG_DEFINED
#define VOXTYPE_CONFIG_DEFINED

struct Config {
    int configVersion = 0;
    std::wstring modelId = L"firered_ctc";
    std::wstring modelDir;
    std::wstring threads = L"auto";
    bool enableVad = false;
    std::wstring vadModel = L"firered";
    float vadThreshold = 0.15f;
    int vadMinSilence = 500;
    int vadMinSpeech = 30;
    int vadPadStart = 150;
    int vadSmoothWindow = 5;
    bool enablePartial = false;
    std::wstring postprocess = L"itn";
    std::wstring hotkey = L"CapsLock";
    bool enableLlm = false;
    std::wstring llmProvider = L"DeepSeek";
    std::wstring llmEndpoint = L"https://api.deepseek.com";
    std::wstring llmApiKey;
    std::wstring llmModel = L"deepseek-v4-flash";
    std::wstring llmPrompt;
    std::wstring llmPromptPreset;
    int llmPromptPresetVersion = 0;
    std::wstring llmExtraParams;
    bool llmVocabularyInjection = true;
    bool enableLlmDebug = false;
    std::wstring llmProvidersJson;
    std::wstring asrBackend = L"local";
    std::wstring fallbackAsrBackend = L"none";
    std::wstring baiduApiKey;
    std::wstring baiduSecretKey;
    int baiduDevPid = 1537;
    std::wstring cloudProvider = L"volcengine";
    std::wstring maiApiProvider = L"openrouter";
    std::wstring maiOpenRouterApiKey;
    std::wstring maiAzureEndpoint;
    std::wstring maiAzureApiKey;
    std::wstring maiLanguage = L"auto";
    std::wstring volcApiKey;
    std::wstring volcResourceId = L"volc.seedasr.sauc.duration";
    std::wstring volcMode = L"bigmodel_nostream";
    std::wstring volcLanguage;
    bool volcEnableNonstream = false;
    int volcEndWindowSize = 800;
    bool volcEnableDdc = false;
    std::wstring volcExtraParams;
    bool volcEnableContext = false;
    int volcContextHistory = 3;
    bool volcEnableInputContext = false;
    bool volcEnableMusicFc = false;
    bool volcEnablePoiFc = false;
    int volcForceToSpeechTime = 0;
    std::wstring volcHotwordsId;
    std::wstring volcHotwordsName;
    std::wstring volcCorrectTableId;
    std::wstring volcCorrectTableName;
    bool volcEnableReuseVocabulary = true;
    std::wstring qwenApiKey;
    std::wstring qwenBaseUrl = kQwenBeijingRealtimeBaseUrl;
    std::wstring qwenHttpBaseUrl = kQwenBeijingHttpBaseUrl;
    std::wstring qwenAudioStreamingBaseUrl = kQwenBeijingAudioStreamingBaseUrl;
    std::wstring qwenModel = L"qwen-audio-3.0-asr-flash-streaming";
    std::wstring qwenTransport = L"audio_streaming";
    std::wstring qwenLanguage;
    int qwenChunkMs = 100;
    std::wstring qwenLanguageHints = kQwenDefaultLanguageHints;
    std::wstring qwenVocabularyId;
    std::wstring qwenVocabulary;
    bool qwenSemanticPunctuation = false;
    int qwenMaxSentenceSilenceMs = 1300;
    bool qwenMultiThresholdMode = false;
    bool qwenHeartbeat = false;
    bool qwenSpeechNoiseThresholdEnabled = false;
    float qwenSpeechNoiseThreshold = 0.0f;
    bool qwenEnableContinueContext = false;
    std::wstring qwenSpecialWordReplaceList;
    std::wstring qwenSpecialWordEmptyList;
    bool qwenSystemReservedFilter = false;
    bool qwenEnableInputContext = false;
    std::wstring qwenInputContextSnapshot;
    bool qwenInputContextSnapshotCaptured = false;
    uint64_t asrAttemptId = 0;
    std::wstring mimoApiKey;
    std::wstring mimoBaseUrl = L"https://token-plan-ams.xiaomimimo.com/v1";
    std::wstring mimoModel = L"mimo-v2.5-asr";
    std::wstring mimoLanguage = L"auto";
    std::wstring doubaoImeDeviceId;
    std::wstring doubaoImeCdid;
    std::wstring doubaoImeToken;
    bool qwenFreePolishEnabled = false;
    bool qwenFreePunctEnabled = false;
    bool qwenFreeCorrectEnabled = false;
    bool qwenFreeRewriteEnabled = false;
    bool qwenFreeDebugLog = false;
    std::wstring qwenFreeShellPath;
    std::wstring qwenFreeUtdidOverride;
    bool enableDebugMode = false;
    bool forceUnicodeInput = false;
    std::wstring audioBackend = L"wasapi";
    std::wstring audioDeviceId;
    std::wstring diagnosticAudioMode = L"off";
    audio_diagnostics::StageKind asrDiagnosticStageKind =
        audio_diagnostics::StageKind::Primary;
    unsigned asrDiagnosticStageIndex = 0;
};

inline void NormalizeQwenFreePostProcessConfig(Config& config) {
    const bool enabled = config.qwenFreePolishEnabled ||
                         config.qwenFreePunctEnabled ||
                         config.qwenFreeCorrectEnabled;
    config.qwenFreePolishEnabled = enabled;
    config.qwenFreePunctEnabled = enabled;
    config.qwenFreeCorrectEnabled = enabled;
}

#endif // VOXTYPE_CONFIG_DEFINED

std::wstring NormalizeDiagnosticAudioMode(std::wstring mode);

std::string ExtractJsonString(const std::string& json, const std::string& key, const std::string& fallback);
bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback);
int ExtractJsonInt(const std::string& json, const std::string& key, int fallback);
float ExtractJsonFloat(const std::string& json, const std::string& key, float fallback);

void SaveCurrentProvider(Config& config);
bool LoadProviderFromStore(Config& config, const std::wstring& name);
int FindPresetIndex(const std::wstring& name);
bool ApplyPreset(Config& config, int index, bool preserveLegacyFields = false);
void LoadConfig(Config& config);
void SaveConfig(const Config& config);

extern Config g_config;
Config& GetGlobalConfig();
