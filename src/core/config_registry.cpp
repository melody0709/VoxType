#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config_registry.h"
#include "llm_refine.h"
#include "utils.h"

#include <cstdio>
#include <limits>
#include <sstream>

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
    size_t valueStart = json.find_first_not_of(" \t\r\n", pos + 1);
    if (valueStart == std::string::npos) return fallback;
    if (json[valueStart] == '"') {
        ++valueStart;
        if (json.compare(valueStart, 4, "true") == 0 && valueStart + 4 < json.size() && json[valueStart + 4] == '"') return true;
        if (json.compare(valueStart, 5, "false") == 0 && valueStart + 5 < json.size() && json[valueStart + 5] == '"') return false;
        if (json.compare(valueStart, 1, "1") == 0 && valueStart + 1 < json.size() && json[valueStart + 1] == '"') return true;
        if (json.compare(valueStart, 1, "0") == 0 && valueStart + 1 < json.size() && json[valueStart + 1] == '"') return false;
        return fallback;
    }
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

namespace config_registry {

Registry& Registry::Instance() {
    static Registry s_instance;
    return s_instance;
}

void Registry::Clear() {
    m_entries.clear();
    m_preSaveHook = nullptr;
    m_postLoadHook = nullptr;
}

void Registry::Register(FieldEntry entry) {
    m_entries.push_back(std::move(entry));
}

void Registry::SetPreSaveHook(LifecycleHook hook) {
    m_preSaveHook = std::move(hook);
}

void Registry::SetPostLoadHook(LifecycleHook hook) {
    m_postLoadHook = std::move(hook);
}

void Registry::LoadJson(Config& cfg, const std::string& json) const {
    for (const auto& entry : m_entries) {
        const std::string key(entry.jsonKey);
        std::visit([&](auto&& memberPtr) {
            using T = std::decay_t<decltype(memberPtr)>;
            if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                const std::string marker = "\"" + key + "\"";
                if (json.find(marker) != std::string::npos) {
                    const std::string raw = ExtractJsonString(json, key, "");
                    std::wstring wide = Utf8ToWide(raw);
                    if (entry.crypto == CryptoPolicy::Dpapi) {
                        if (!wide.empty()) {
                            std::wstring decrypted = llm::DecryptString(wide);
                            // Fallback to raw if DPAPI decrypt returns empty (e.g. unencrypted legacy format)
                            cfg.*memberPtr = decrypted.empty() ? wide : decrypted;
                        } else {
                            cfg.*memberPtr = L"";
                        }
                    } else {
                        cfg.*memberPtr = wide;
                    }
                }
            } else if constexpr (std::is_same_v<T, bool Config::*>) {
                cfg.*memberPtr = ExtractJsonBool(json, key, cfg.*memberPtr);
            } else if constexpr (std::is_same_v<T, int Config::*>) {
                cfg.*memberPtr = ExtractJsonInt(json, key, cfg.*memberPtr);
            } else if constexpr (std::is_same_v<T, float Config::*>) {
                cfg.*memberPtr = ExtractJsonFloat(json, key, cfg.*memberPtr);
            }
        }, entry.member);
    }

    if (m_postLoadHook) {
        m_postLoadHook(cfg);
    }
}

std::string Registry::SaveJson(const Config& cfg) const {
    Config cfgCopy = cfg;
    if (m_preSaveHook) {
        m_preSaveHook(cfgCopy);
    }

    std::ostringstream ss;
    ss << "{\n";

    for (size_t i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        ss << "  \"" << entry.jsonKey << "\": ";

        std::visit([&](auto&& memberPtr) {
            using T = std::decay_t<decltype(memberPtr)>;
            if constexpr (std::is_same_v<T, std::wstring Config::*>) {
                const std::wstring& val = cfgCopy.*memberPtr;
                if (entry.crypto == CryptoPolicy::Dpapi) {
                    ss << "\"" << EscapeJson(llm::EncryptString(val)) << "\"";
                } else {
                    ss << "\"" << EscapeJson(val) << "\"";
                }
            } else if constexpr (std::is_same_v<T, bool Config::*>) {
                const bool val = cfgCopy.*memberPtr;
                switch (entry.boolFormat) {
                case BoolJsonFormat::Literal:
                    ss << (val ? "true" : "false");
                    break;
                case BoolJsonFormat::QuotedInt:
                    ss << (val ? "\"1\"" : "\"0\"");
                    break;
                case BoolJsonFormat::RawInt:
                    ss << (val ? "1" : "0");
                    break;
                }
            } else if constexpr (std::is_same_v<T, int Config::*>) {
                ss << (cfgCopy.*memberPtr);
            } else if constexpr (std::is_same_v<T, float Config::*>) {
                if (entry.floatPrecision >= 0) {
                    char buf[64] = {};
                    std::snprintf(buf, sizeof(buf), "%.*f", entry.floatPrecision, cfgCopy.*memberPtr);
                    ss << buf;
                } else {
                    const auto prevPrec = ss.precision(std::numeric_limits<float>::max_digits10);
                    ss << (cfgCopy.*memberPtr);
                    ss.precision(prevPrec);
                }
            }
        }, entry.member);

        if (i + 1 < m_entries.size()) {
            ss << ",\n";
        } else {
            ss << "\n";
        }
    }

    ss << "}\n";
    return ss.str();
}

void InitializeRegistry() {
    auto& reg = Registry::Instance();
    reg.Clear();

    // Register all persistent fields in canonical serialization order
    reg.Register({"config_version", &Config::configVersion});
    reg.Register({"model_id", &Config::modelId});
    reg.Register({"model_dir", &Config::modelDir});
    reg.Register({"threads", &Config::threads});
    reg.Register({"enable_vad", &Config::enableVad, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"vad_model", &Config::vadModel});
    reg.Register({"vad_threshold", &Config::vadThreshold});
    reg.Register({"vad_min_silence", &Config::vadMinSilence});
    reg.Register({"vad_min_speech", &Config::vadMinSpeech});
    reg.Register({"vad_pad_start", &Config::vadPadStart});
    reg.Register({"vad_smooth_window", &Config::vadSmoothWindow});
    reg.Register({"enable_partial", &Config::enablePartial, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"postprocess", &Config::postprocess});
    reg.Register({"hotkey", &Config::hotkey});
    reg.Register({"llm_provider", &Config::llmProvider});
    reg.Register({"llm_providers_json", &Config::llmProvidersJson});
    reg.Register({"llm_endpoint", &Config::llmEndpoint});
    reg.Register({"llm_api_key", &Config::llmApiKey, CryptoPolicy::Dpapi});
    reg.Register({"llm_model", &Config::llmModel});
    reg.Register({"llm_prompt", &Config::llmPrompt});
    reg.Register({"enable_llm_debug", &Config::enableLlmDebug, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"enable_debug_mode", &Config::enableDebugMode, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"force_unicode_input", &Config::forceUnicodeInput, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"asr_backend", &Config::asrBackend});
    reg.Register({"fallback_asr_backend", &Config::fallbackAsrBackend});
    reg.Register({"baidu_api_key", &Config::baiduApiKey});
    reg.Register({"baidu_secret_key", &Config::baiduSecretKey, CryptoPolicy::Dpapi});
    reg.Register({"baidu_dev_pid", &Config::baiduDevPid});
    reg.Register({"cloud_provider", &Config::cloudProvider});
    reg.Register({"mai_api_provider", &Config::maiApiProvider});
    reg.Register({"mai_openrouter_api_key", &Config::maiOpenRouterApiKey, CryptoPolicy::Dpapi});
    reg.Register({"mai_azure_endpoint", &Config::maiAzureEndpoint});
    reg.Register({"mai_azure_api_key", &Config::maiAzureApiKey, CryptoPolicy::Dpapi});
    reg.Register({"mai_language", &Config::maiLanguage});
    reg.Register({"volc_api_key", &Config::volcApiKey, CryptoPolicy::Dpapi});
    reg.Register({"volc_resource_id", &Config::volcResourceId});
    reg.Register({"volc_mode", &Config::volcMode});
    reg.Register({"volc_language", &Config::volcLanguage});
    reg.Register({"volc_enable_nonstream", &Config::volcEnableNonstream, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_end_window_size", &Config::volcEndWindowSize});
    reg.Register({"volc_enable_ddc", &Config::volcEnableDdc, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_extra_params", &Config::volcExtraParams});
    reg.Register({"volc_enable_context", &Config::volcEnableContext, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_context_history", &Config::volcContextHistory});
    reg.Register({"volc_enable_input_context", &Config::volcEnableInputContext, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_enable_music_fc", &Config::volcEnableMusicFc, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_enable_poi_fc", &Config::volcEnablePoiFc, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"volc_force_to_speech_time", &Config::volcForceToSpeechTime});
    reg.Register({"volc_hotwords_id", &Config::volcHotwordsId});
    reg.Register({"volc_hotwords_name", &Config::volcHotwordsName});
    reg.Register({"volc_correct_table_id", &Config::volcCorrectTableId});
    reg.Register({"volc_correct_table_name", &Config::volcCorrectTableName});
    reg.Register({"qwen_api_key", &Config::qwenApiKey, CryptoPolicy::Dpapi});
    reg.Register({"qwen_base_url", &Config::qwenBaseUrl});
    reg.Register({"qwen_http_base_url", &Config::qwenHttpBaseUrl});
    reg.Register({"qwen_audio_streaming_base_url", &Config::qwenAudioStreamingBaseUrl});
    reg.Register({"qwen_model", &Config::qwenModel});
    reg.Register({"qwen_transport", &Config::qwenTransport});
    reg.Register({"qwen_language", &Config::qwenLanguage});
    reg.Register({"qwen_chunk_ms", &Config::qwenChunkMs});
    reg.Register({"qwen_language_hints", &Config::qwenLanguageHints});
    reg.Register({"qwen_vocabulary_id", &Config::qwenVocabularyId});
    reg.Register({"qwen_vocabulary", &Config::qwenVocabulary});
    reg.Register({"qwen_semantic_punctuation", &Config::qwenSemanticPunctuation, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_max_sentence_silence", &Config::qwenMaxSentenceSilenceMs});
    reg.Register({"qwen_multi_threshold", &Config::qwenMultiThresholdMode, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_heartbeat", &Config::qwenHeartbeat, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_speech_noise_threshold_enabled", &Config::qwenSpeechNoiseThresholdEnabled, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_speech_noise_threshold", &Config::qwenSpeechNoiseThreshold, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_enable_input_context", &Config::qwenEnableInputContext, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_enable_continue_context", &Config::qwenEnableContinueContext, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"qwen_special_word_replace", &Config::qwenSpecialWordReplaceList});
    reg.Register({"qwen_special_word_empty", &Config::qwenSpecialWordEmptyList});
    reg.Register({"qwen_system_reserved_filter", &Config::qwenSystemReservedFilter, CryptoPolicy::None, BoolJsonFormat::Literal});
    reg.Register({"mimo_api_key", &Config::mimoApiKey, CryptoPolicy::Dpapi});
    reg.Register({"mimo_base_url", &Config::mimoBaseUrl});
    reg.Register({"mimo_model", &Config::mimoModel});
    reg.Register({"mimo_language", &Config::mimoLanguage});
    reg.Register({"doubao_ime_device_id", &Config::doubaoImeDeviceId});
    reg.Register({"doubao_ime_cdid", &Config::doubaoImeCdid});
    reg.Register({"doubao_ime_token", &Config::doubaoImeToken, CryptoPolicy::Dpapi});
    reg.Register({"qwen_free_polish", &Config::qwenFreePolishEnabled, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"qwen_free_punct", &Config::qwenFreePunctEnabled, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"qwen_free_correct", &Config::qwenFreeCorrectEnabled, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"qwen_free_rewrite", &Config::qwenFreeRewriteEnabled, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"qwen_free_debug_log", &Config::qwenFreeDebugLog, CryptoPolicy::None, BoolJsonFormat::RawInt});
    reg.Register({"qwen_free_shell_path", &Config::qwenFreeShellPath});
    reg.Register({"qwen_free_utdid_override", &Config::qwenFreeUtdidOverride, CryptoPolicy::Dpapi});
    reg.Register({"audio_backend", &Config::audioBackend});
    reg.Register({"audio_device_id", &Config::audioDeviceId});
    reg.Register({"diagnostic_audio_mode", &Config::diagnosticAudioMode});
}

} // namespace config_registry
