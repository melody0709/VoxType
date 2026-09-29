#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <memory>

#include "qwen_audio_profile.h"

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
    // Fresh installs start on the Audio 3.1 message generation; the constant
    // lives in qwen_audio_profile.h so the model name is written once. Existing
    // config files keep their selected model (NormalizePersistedProfile).
    std::wstring qwenModel = qwen_audio_profile::kMessageModel31;
    std::wstring qwenTransport = L"audio_streaming";
    std::wstring qwenLanguage;
    int qwenChunkMs = 100;
    std::wstring qwenLanguageHints = kQwenDefaultLanguageHints;
    std::wstring qwenVocabularyId;
    std::wstring qwenVocabulary;
    bool qwenSemanticPunctuation = false;
    int qwenMaxSentenceSilenceMs = 1300;
    bool qwenMultiThresholdMode = false;
    // On by default so the duplex connection stays warm between recordings.
    bool qwenHeartbeat = true;
    bool qwenSpeechNoiseThresholdEnabled = false;
    float qwenSpeechNoiseThreshold = 0.0f;
    bool qwenEnableContinueContext = true;
    std::wstring qwenSpecialWordReplaceList;
    std::wstring qwenSpecialWordEmptyList;
    bool qwenSystemReservedFilter = false;
    // Audio 3.1 duplex only (3.1 streaming + 3.1 message): SupportsVadModel().
    // The default mirrors the provider default so a 3.0 recording keeps its
    // previous microphone posture.
    std::wstring qwenVadModel = L"far_field_meeting_16k";
    // The whole Audio 3.1 generation (streaming + message + HTTP batch):
    // SupportsKeepDialect(). On by default so dialect speech keeps its original
    // wording; turning it off transcribes dialects into standard Mandarin.
    bool qwenKeepDialect = true;
    // qwen-audio-3.1-asr-flash-message only: filters filler words and polishes
    // the transcript. On by default; turning it off keeps the raw wording.
    bool qwenDisfluencyRemovalEnabled = true;
    bool qwenEnableInputContext = true;
    std::wstring qwenInputContextSnapshot;
    bool qwenInputContextSnapshotCaptured = false;
    // Multi-turn context enhancement: recent recognition results (and the
    // vocabulary as a domain word list when nothing else is available) are sent
    // as extra user turns. On by default; turning it off stops forwarding
    // previous transcripts to the cloud (the volcEnableContext posture).
    bool qwenHistoryContext = true;
    int qwenHistoryContextRounds = 3;
    // Runtime only (never persisted): the sensitive-control probe started before
    // the focused-field read of this recording
    // (input_context::BeginSensitiveFocusProbe). Its answer is read when the final
    // transcript is recorded, because the history is sendable to the cloud and a
    // transcript dictated into a password control must never enter it — not even
    // later, after the user enables a context feature. The mapping is
    // fail-closed (input_context::MustSkipHistoryForFocus): an unverified focus
    // also refuses the write, which only costs later context enhancement.
    // Shared ownership is intentional: Config is copied by value into every
    // session/attempt snapshot and must keep observing one answer.
    std::shared_ptr<std::atomic<int>> asrSensitiveProbe;
    // Runtime only (never persisted): the turns assembled for this attempt,
    // oldest first. The focused-field text travels separately in
    // qwenInputContextSnapshot so the continue-task refresh can keep the history.
    std::vector<std::wstring> qwenContextHistoryTurns;
    uint64_t asrAttemptId = 0;
    std::wstring mimoApiKey;
    std::wstring mimoBaseUrl = L"https://api.xiaomimimo.com/v1";
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
    bool restoreClipboardAfterPaste = true;
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
