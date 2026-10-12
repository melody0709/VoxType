#pragma once

// Wire StartSession payload for the doubao_ime backend (header-only, pure).
//
// Extracted from doubao_ime_asr.cpp so the exact bytes can be frozen by an
// offline regression test: the payload is the only place where a mistake would
// break every doubao session, and it is shared by all doubao runs, so it must be
// covered without touching the network or the heavy client TU.
//
// Contract for `deviceIdEscaped`: the caller passes the already JSON-escaped
// UTF-8 device id (existing EscapeJson behaviour, unchanged). `contextBase64`
// and `contextOverride.base64` are inserted verbatim; base64 needs no escaping.

#include <sstream>
#include <string>

namespace doubao_ime_payload {

struct Fields {
    int channels = 1;
    int sampleRate = 16000;
    bool enablePunctuation = true;
    std::string deviceIdEscaped;
    // AsrContext base64 carried in extra["context"]; empty keeps the payload
    // byte-identical to a session without context enhancement (the default).
    std::string contextBase64;
};

// Diagnostic override used by the S4 carrier experiment hook (environment
// gated by the caller). Kept separate from Fields so the production path cannot
// be reached through it by accident: `active` must be set explicitly.
struct ContextOverride {
    enum class Slot {
        Extra,         // ,"context":"<base64>" inside the extra object
        TopLevel,      // ,"context":"<base64>" next to extra
        MalformedJson, // unterminated string: must be rejected by the server
        WrongType,     // "context":12345
    };
    bool active = false;
    Slot slot = Slot::Extra;
    std::string base64;
    // When non-empty, replaces the whole extra object body (official-equivalent
    // carrier probes).
    std::string extraBodyOverride;
};

inline std::string BuildStartSessionPayload(const Fields& fields,
                                            const ContextOverride& override = ContextOverride{}) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"audio_info\":{";
    oss << "\"channel\":" << fields.channels << ",";
    oss << "\"format\":\"speech_opus\",";
    oss << "\"sample_rate\":" << fields.sampleRate;
    oss << "},";
    oss << "\"enable_punctuation\":" << (fields.enablePunctuation ? "true" : "false") << ",";
    oss << "\"enable_speech_rejection\":false,";

    const bool overrideActive = override.active;
    const bool overrideInExtra =
        overrideActive && override.slot != ContextOverride::Slot::TopLevel;

    oss << "\"extra\":{";
    if (overrideInExtra && !override.extraBodyOverride.empty()) {
        oss << override.extraBodyOverride;
    } else {
        oss << "\"app_name\":\"com.android.chrome\",";
        oss << "\"cell_compress_rate\":8,";
        oss << "\"did\":\"" << fields.deviceIdEscaped << "\",";
        oss << "\"enable_asr_threepass\":true,";
        oss << "\"enable_asr_twopass\":true,";
        oss << "\"input_mode\":\"tool\"";
        // Official carrier (E-016/E-018): the SDK copies the client `extra`
        // object verbatim into the wire payload, so the merged AsrContext rides
        // in extra["context"]. A diagnostic override for this slot OWNS the key
        // whenever it emits one: emitting both would duplicate the JSON key (and
        // the malformed control would truncate the object, making the server
        // report "failed to merge field payload" for an unrelated reason).
        const bool overrideEmitsContext =
            overrideInExtra && (override.slot != ContextOverride::Slot::Extra ||
                                !override.base64.empty());
        if (!fields.contextBase64.empty() && !overrideEmitsContext) {
            oss << ",\"context\":\"" << fields.contextBase64 << "\"";
        }
        if (overrideInExtra) {
            switch (override.slot) {
            case ContextOverride::Slot::MalformedJson:
                // Unterminated string on purpose: only a server that really
                // parses this payload can reject it.
                oss << ",\"context\":\"" << override.base64;
                break;
            case ContextOverride::Slot::WrongType:
                oss << ",\"context\":12345";
                break;
            case ContextOverride::Slot::Extra:
            case ContextOverride::Slot::TopLevel:
                if (!override.base64.empty()) {
                    oss << ",\"context\":\"" << override.base64 << "\"";
                }
                break;
            }
        }
    }
    oss << "}";
    if (overrideActive && override.slot == ContextOverride::Slot::TopLevel) {
        oss << ",\"context\":\"" << override.base64 << "\"";
    }
    oss << "}";
    return oss.str();
}

} // namespace doubao_ime_payload
