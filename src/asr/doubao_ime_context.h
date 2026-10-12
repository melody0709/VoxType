#pragma once

// AsrContext assembly for the doubao_ime backend (P1).
//
// The official Windows IME uploads a base64 AsrContext inside the StartSession
// payload's `extra` object (evidence E-016 section 3, confirmed at the SDK level
// in E-018: audioeffect-mt.dll copies the client `extra` JSON verbatim into the
// wire payload). Field semantics come from E-015/E-017:
//
//   chat[]       {"type":"user_input",
//                 "data":"{\"cursor_position\":N,\"text\":\"...\"}",   (double encoded)
//                 "time":<unix seconds>}
//                N is the code-point count of that turn (the official value is
//                "characters before the caret"); a trailing newline is dropped.
//   hotwords[]   {"word":W,"class":"user","frequency":0}
//                The official client always sends frequency 0 (E-017 B4).
//   trackingInfo {"start_type":"shortcut_long"}
//                Official values are shortcut_single / shortcut_long (E-017 B1).
//
// Privacy: ime_info, loc_info and app_apk_name are deliberately not emitted —
// they carry the host application name and the public IP, and the server
// tolerates their absence (E-018 measured an accepted session without them).
//
// The feature is opt-in (Config::doubaoContextEnabled defaults to false): the
// E-018 experiment showed the carrier is accepted but produced no observable
// effect on our device identity, so uploading user text must be a user choice.

#include <cstddef>
#include <string>
#include <vector>

struct Config;

namespace doubao_ime_context {

struct ContextOptions {
    size_t maxHistoryTurns = 5;    // official keeps at most five context messages
    size_t maxHistoryChars = 400;  // per turn, tail-kept (the caret side survives)
    size_t maxHotwords = 100;
    bool includeStartType = true;  // trackingInfo.start_type = shortcut_long
};

// Builds the AsrContext JSON for the given chronological history (oldest first).
std::string BuildAsrContextJson(const std::vector<std::wstring>& history,
                                const Config& config,
                                const ContextOptions& options = ContextOptions{});

// Same, wrapped in standard base64 (padded) as it travels in extra["context"].
std::string BuildAsrContextBase64(const std::vector<std::wstring>& history,
                                  const Config& config,
                                  const ContextOptions& options = ContextOptions{});

// Pure hotword array renderer: {"word":W,"class":"user","frequency":0}, in the
// given order, deduplicated, capped by options.maxHotwords. Split out of the
// context builder so the wire shape is testable without touching whichever
// vocabulary file the machine happens to have.
std::string BuildHotwordsJson(const std::vector<std::wstring>& words,
                              const ContextOptions& options = ContextOptions{});

// Exposed for the protocol regression test.
std::string EncodeBase64(const std::string& bytes);
size_t CountCodePoints(const std::wstring& text);

} // namespace doubao_ime_context
