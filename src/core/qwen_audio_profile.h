#pragma once

#include <string>

namespace qwen_audio_profile {

inline constexpr wchar_t kLegacyModel[] = L"qwen3-asr-flash-realtime";
inline constexpr wchar_t kHttpModel[] = L"qwen-audio-3.0-asr-flash";
inline constexpr wchar_t kStreamingModel[] = L"qwen-audio-3.0-asr-flash-streaming";
inline constexpr wchar_t kHttpModel31[] = L"qwen-audio-3.1-asr-flash";
inline constexpr wchar_t kStreamingModel31[] = L"qwen-audio-3.1-asr-flash-streaming";
inline constexpr wchar_t kMessageModel31[] = L"qwen-audio-3.1-asr-flash-message";

inline constexpr wchar_t kLegacyTransport[] = L"legacy_realtime";
inline constexpr wchar_t kHttpTransport[] = L"audio_http";
inline constexpr wchar_t kStreamingTransport[] = L"audio_streaming";

inline bool IsHttpModel(const std::wstring& model) {
    return model == kHttpModel || model == kHttpModel31;
}

inline bool IsStreamingModel(const std::wstring& model) {
    return model == kStreamingModel || model == kStreamingModel31 || model == kMessageModel31;
}

inline bool IsSupportedModel(const std::wstring& model) {
    return model == kLegacyModel || IsHttpModel(model) || IsStreamingModel(model);
}

inline bool IsHttpModel31(const std::wstring& model) {
    return model == kHttpModel31;
}

inline bool IsStreamingModel31(const std::wstring& model) {
    return model == kStreamingModel31;
}

// qwen-audio-3.1-asr-flash-message speaks the same /api-ws/v1/inference duplex
// protocol as the streaming family, but it rejects language_hints,
// semantic_punctuation_enabled, multi_threshold_mode_enabled and
// special_word_filter, and it accepts disfluency_removal_enabled plus
// intermediate_result_enabled instead.
inline bool IsMessageModel(const std::wstring& model) {
    return model == kMessageModel31;
}

inline bool SupportsLanguageHints(const std::wstring& model) {
    return !IsMessageModel(model);
}

inline bool SupportsSemanticPunctuation(const std::wstring& model) {
    return !IsMessageModel(model);
}

inline bool SupportsSpecialWordFilter(const std::wstring& model) {
    return !IsMessageModel(model);
}

inline bool SupportsDisfluencyRemoval(const std::wstring& model) {
    return IsMessageModel(model);
}

inline bool SupportsIntermediateResult(const std::wstring& model) {
    return IsMessageModel(model);
}

// run-task.payload.parameters.vad_model exists only on the 3.1 generation
// (near_meeting_16k / far_field_meeting_16k): 3.1 streaming and 3.1 message.
// 3.0 streaming rejects the field, so the sender must stay silent there.
inline bool SupportsVadModel(const std::wstring& model) {
    return IsStreamingModel31(model) || IsMessageModel(model);
}

// keep_dialect is documented for the whole 3.1 generation: the streaming
// run-task parameters (3.1 streaming and 3.1 message) and the HTTP batch
// parameters both accept it.
inline bool SupportsKeepDialect(const std::wstring& model) {
    return IsHttpModel31(model) || IsStreamingModel31(model) || IsMessageModel(model);
}

inline void NormalizePersistedProfile(std::wstring& model,
                                      std::wstring& transport,
                                      bool hasPersistedModel,
                                      bool hasPersistedTransport) {
    // A config file predating the model selector must keep the pre-Audio-3
    // behavior. The Config struct's streaming default is only for a fresh
    // install, where LoadConfig() is not entered because the file is absent.
    if (!hasPersistedModel) {
        model = kLegacyModel;
        transport = kLegacyTransport;
        return;
    }

    if (IsHttpModel(model)) {
        transport = kHttpTransport;
    } else if (IsStreamingModel(model)) {
        transport = kStreamingTransport;
    } else if (model == kLegacyModel) {
        transport = kLegacyTransport;
    } else {
        // Unknown model names must not inherit an unrelated transport.
        model = kLegacyModel;
        transport = kLegacyTransport;
        return;
    }

    (void)hasPersistedTransport;
}

} // namespace qwen_audio_profile
