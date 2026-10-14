#pragma once
// Browser companion bridge.
//
// WHAT THIS IS
// ------------
// The accessibility-free replacement for reading the focused field of a browser.
// Chromium keeps a PROCESS-wide accessibility mode once any platform consumer
// asks for it (see browser_target.h), and releasing our UIA/MSAA objects does not
// close it. A Manifest V3 companion extension can read `document.activeElement`
// directly instead, which never touches the accessibility tree, so this module
// carries that channel:
//
//   VoxType.exe (tray)  <-- named pipe, current user + session only -->
//   VoxType.exe --native-host (this same EXE, restricted mode)
//                       <-- official length-prefixed JSON over stdio -->
//   extension service worker
//                       <-- chrome.scripting.executeScript (on demand) -->
//   the focused field of the active tab
//
// Two rules are structural here, not conventions:
//   1. The host mode runs BEFORE the single-instance mutex, because the desktop
//      application is normally already running when a browser launches us. That
//      is why main.cpp calls RunNativeHostIfInvoked() as its first statement.
//   2. A failed lookup NEVER falls back to UIA/MSAA. Callers get `ok == false`
//      plus a reason and must degrade to "no context, fail-closed privacy".
//
// The desktop side is deliberately passive: one pipe instance, one bounded
// request/response, no polling and no per-page work while nobody is recording.

#include "browser_target.h"
#include "utils.h"

#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace browser_bridge {

// ---------------------------------------------------------------------------
// Protocol constants and message shape
// ---------------------------------------------------------------------------

inline constexpr int kProtocolVersion = 1;
inline constexpr size_t kMaxMessageBytes = 65536;      // hard cap on one frame
inline constexpr size_t kMaxWindows = 128;             // windows advertised in `hello`
inline constexpr size_t kMaxPeers = 8;                 // one per browser profile/instance
inline constexpr size_t kMaxValueCharacters = 400;     // matches qwen_context::kMaxContextCharacters
inline constexpr wchar_t kHostName[] = L"com.voxtype.browser_bridge";
// Pinned by the `key` field of extensions/browser-bridge/manifest.json, so it is
// the same for a development load and a store build. The host manifest shipped in
// packaging/native-messaging/ lists `chrome-extension://<id>/` as its only
// allowed origin, and the host additionally verifies the id the worker reports.
inline constexpr char kExtensionIdUtf8[] = "cjmhlnmnolcbkjpfdbnpejfnigacbpai";

inline constexpr const char* kTypeHello = "hello";
inline constexpr const char* kTypeQuery = "query";
inline constexpr const char* kTypeResult = "result";
inline constexpr const char* kTypeUnavailable = "unavailable";

// Failure reasons. Diagnostics use these verbatim; they are never shown to the
// user as-is; they are recorded in the debug console/runtime log.
//
// They are deliberately fine-grained: "the companion is not connected" and "the
// companion is connected but advertises no window matching the one we anchored"
// look identical in the UI but have completely different causes, and collapsing
// them into one string once cost a full debugging round.
inline constexpr const char* kReasonNotConnected = "not_connected";      // no peer at all
inline constexpr const char* kReasonNoWindowMatch = "no_window_match";   // peer(s), no anchored window
inline constexpr const char* kReasonWindowAmbiguous = "window_ambiguous";  // 2+ windows claim the anchor
inline constexpr const char* kReasonNoAnchor = "no_anchor";              // caller passed no HWND
inline constexpr const char* kReasonTargetUnavailable = "target_unavailable";
inline constexpr const char* kReasonTimeout = "timeout";
inline constexpr const char* kReasonInvalidMessage = "invalid_message";
inline constexpr const char* kReasonDisconnected = "disconnected";

// What the desktop asks for. Kept to the two things that used to need UIA:
// the fail-closed password answer, and the focused field's own text.
enum class Need { Probe, Field };

// ---------------------------------------------------------------------------
// Framing: 4-byte little-endian length prefix + UTF-8 JSON, on both transports.
// The named pipe speaks the same frame format as Native Messaging, so the host
// process relays without translating.
// ---------------------------------------------------------------------------

namespace detail {

enum class PipeIo { Synchronous, Overlapped };

// Literal readers for this protocol's own messages. They are module-local on
// purpose: the project already has three global ExtractJson* families
// (engine.cpp, qwen_free_proto_llm.cpp, utils.h) and adding a fourth global one
// is exactly what the repository rules forbid. Both go through the shared
// escape-aware `json_detail` scanning helpers, so there is one JSON tokenizer.
inline long long JsonInt(const std::string& json, const std::string& key,
                         long long fallback) {
    if (!json_detail::IsValidDocument(json)) return fallback;
    size_t pos = json_detail::FindValueForKey(json, key);
    if (pos == std::string::npos || pos >= json.size()) return fallback;
    const bool negative = json[pos] == '-';
    if (negative) ++pos;
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return fallback;
    long long value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        if (value > 1000000000000LL) return fallback;  // refuse to overflow on junk
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    if (pos < json.size() && (json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E')) return fallback;
    return negative ? -value : value;
}

inline std::optional<bool> JsonBool(const std::string& json, const std::string& key) {
    if (!json_detail::IsValidDocument(json)) return std::nullopt;
    const size_t pos = json_detail::FindValueForKey(json, key);
    if (pos == std::string::npos || pos + 4 > json.size()) return std::nullopt;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return std::nullopt;
}

// Blocking loop for a synchronous handle, with no deadline of its own.
inline bool TransferSyncBlocking(HANDLE pipe, std::span<char> bytes, bool write) {
    while (!bytes.empty()) {
        DWORD count = 0;
        const BOOL ok = write ? WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)
                              : ReadFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
        if (!ok || count == 0) return false;
        bytes = bytes.subspan(count);
    }
    return true;
}

// A synchronous pipe handle cannot take an OVERLAPPED structure, so a plain
// ReadFile/WriteFile ignores the deadline entirely - which made the host's
// "10 s for hello" budget a lie and let a silent peer pin the host process
// forever. The blocking call therefore runs on a helper thread and is cancelled
// with CancelSynchronousIo, the documented way to abort pending *synchronous*
// I/O from another thread (the call then completes with ERROR_OPERATION_ABORTED).
inline bool TransferSyncBounded(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs) {
    if (timeoutMs == INFINITE) return TransferSyncBlocking(pipe, bytes, write);
    const HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!finished) return false;
    std::atomic<bool> transferred{false};
    std::jthread worker([&] {
        transferred.store(TransferSyncBlocking(pipe, bytes, write), std::memory_order_relaxed);
        SetEvent(finished);
    });
    const bool inTime = WaitForSingleObject(finished, timeoutMs) == WAIT_OBJECT_0;
    if (!inTime) {
        // Cancellation can race the worker entering its next ReadFile/WriteFile.
        do {
            CancelSynchronousIo(worker.native_handle());
        } while (WaitForSingleObject(finished, 5) == WAIT_TIMEOUT);
    }
    worker.join();  // the worker must be done before its event handle is closed
    const bool result = inTime && transferred.load(std::memory_order_relaxed);
    CloseHandle(finished);
    return result;
}

// Overlapped path: the handle was opened with FILE_FLAG_OVERLAPPED, so the
// deadline is enforced by waiting on the operation's own event.
inline bool TransferOverlapped(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs,
                               HANDLE cancelled = nullptr) {
    std::optional<ULONGLONG> deadline;
    if (timeoutMs != INFINITE) deadline = GetTickCount64() + timeoutMs;
    while (!bytes.empty()) {
        if (cancelled && WaitForSingleObject(cancelled, 0) != WAIT_TIMEOUT) return false;
        DWORD count = 0;
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        const BOOL ok = write ? WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, &ov)
                              : ReadFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, &ov);
        bool completed = ok != FALSE;
        if (!completed && GetLastError() == ERROR_IO_PENDING) {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining =
                !deadline ? INFINITE : now < *deadline ? static_cast<DWORD>(*deadline - now) : 0;
            HANDLE events[] = {ov.hEvent, cancelled};
            if (WaitForMultipleObjects(cancelled ? 2 : 1, events, FALSE, remaining) == WAIT_OBJECT_0) {
                completed = GetOverlappedResult(pipe, &ov, &count, FALSE) != FALSE;
            } else {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &count, TRUE);
                completed = false;
            }
        }
        CloseHandle(ov.hEvent);
        if (!completed || count == 0) return false;
        bytes = bytes.subspan(count);
    }
    return true;
}

inline bool Transfer(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs, PipeIo io,
                     HANDLE cancelled = nullptr) {
    return io == PipeIo::Synchronous ? TransferSyncBounded(pipe, bytes, write, timeoutMs)
                                     : TransferOverlapped(pipe, bytes, write, timeoutMs, cancelled);
}

// Structural sanity for an incoming frame. Field-level validation happens in the
// typed extractors below; this rejects non-JSON, unknown versions and unknown
// message types before anything else looks at the bytes.
inline bool IsWellFormedFrame(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > kMaxMessageBytes) return false;
    const std::string text(bytes);
    if (!json_detail::IsValidDocument(text)) return false;
    if (detail::JsonInt(text, "version", -1) != kProtocolVersion) return false;
    const std::wstring type = ExtractJsonStringDecoded(text, "type");
    if (type.empty()) return false;
    const std::string narrow = WideToUtf8(type);
    return narrow == kTypeHello || narrow == kTypeQuery || narrow == kTypeResult ||
           narrow == kTypeUnavailable;
}

inline bool WriteFrame(HANDLE pipe, const std::string& json, DWORD timeoutMs, PipeIo io) {
    if (!IsWellFormedFrame(json)) return false;
    const uint32_t size = static_cast<uint32_t>(json.size());
    char header[4] = {static_cast<char>(size & 0xFF), static_cast<char>((size >> 8) & 0xFF),
                      static_cast<char>((size >> 16) & 0xFF), static_cast<char>((size >> 24) & 0xFF)};
    if (!Transfer(pipe, std::span<char>(header, 4), true, timeoutMs, io)) return false;
    return Transfer(pipe, std::span<char>(const_cast<char*>(json.data()), json.size()), true, timeoutMs, io);
}

inline std::expected<std::string, std::string> ReadFrame(HANDLE pipe, DWORD timeoutMs, PipeIo io,
                                                       HANDLE cancelled = nullptr) {
    const ULONGLONG start = GetTickCount64();
    const auto remaining_from = [&](ULONGLONG began) -> DWORD {
        if (timeoutMs == INFINITE) return INFINITE;
        const ULONGLONG elapsed = GetTickCount64() - began;
        return elapsed < timeoutMs ? static_cast<DWORD>(timeoutMs - elapsed) : 0;
    };
    char header[4] = {};
    if (!Transfer(pipe, std::span<char>(header, 4), false, timeoutMs, io, cancelled)) {
        return std::unexpected(kReasonDisconnected);
    }
    const auto* raw = reinterpret_cast<const unsigned char*>(header);
    const uint32_t size = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
                          (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
    if (size == 0 || size > kMaxMessageBytes) return std::unexpected(kReasonInvalidMessage);
    std::string bytes(size, '\0');
    if (!Transfer(pipe, std::span<char>(bytes.data(), bytes.size()), false, remaining_from(start), io, cancelled)) {
        return std::unexpected(kReasonDisconnected);
    }
    if (!IsWellFormedFrame(bytes)) return std::unexpected(kReasonInvalidMessage);
    return bytes;
}

// JSON string literal (with quotes) for a wide string.
inline std::string JsonString(const std::wstring& value) {
    return "\"" + EscapeJson(value) + "\"";
}

inline std::string JsonString(const std::string& value) { return JsonString(Utf8ToWide(value)); }

}  // namespace detail

// ---------------------------------------------------------------------------
// Pipe identity: name + DACL + readiness event are derived from the current
// user's SID and logon session, so only a same-user, same-session process can
// connect, and remote clients are rejected by the pipe itself.
// ---------------------------------------------------------------------------

struct PipeIdentity {
    std::wstring name;
    std::wstring sddl;
    std::wstring readyName;
};

inline std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> bytes(size);
    const bool ok = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!ok) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return {};
    std::wstring text(sid);
    LocalFree(sid);
    return text;
}

inline DWORD CurrentSessionId() {
    DWORD session = 0;
    return ProcessIdToSessionId(GetCurrentProcessId(), &session) ? session : 0;
}

inline PipeIdentity CurrentIdentity() {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return {};
    const DWORD session = CurrentSessionId();
    PipeIdentity identity;
    identity.name = std::format(L"\\\\.\\pipe\\VoxType.BrowserBridge.{}.{}", sid, session);
    // Owner gets full access and nothing else: the default DACL would also grant
    // access to other same-account tokens, which is acceptable, but an explicit
    // single-ACE DACL keeps the contract readable and remote clients rejected.
    identity.sddl = std::format(L"D:P(A;;GA;;;{})", sid);
    identity.readyName = std::format(L"Local\\VoxType.BrowserBridge.Ready.{}.{}", sid, session);
    return identity;
}

inline HANDLE CreateReadyEvent(const PipeIdentity& identity) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(identity.sddl.c_str(), SDDL_REVISION_1,
                                                             &descriptor, nullptr)) {
        return nullptr;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE event = CreateEventW(&security, TRUE, FALSE, identity.readyName.c_str());
    LocalFree(descriptor);
    return event;
}

inline std::wstring ProcessImagePath(DWORD pid) {
    if (pid == 0) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size);
    return path;
}

inline bool SameSession(DWORD pid) {
    DWORD ours = 0;
    DWORD theirs = 0;
    return ProcessIdToSessionId(GetCurrentProcessId(), &ours) && ProcessIdToSessionId(pid, &theirs) &&
           ours == theirs;
}

inline bool SameImageAsSelf(DWORD pid) {
    const std::wstring theirs = ProcessImagePath(pid);
    return !theirs.empty() && theirs == ProcessImagePath(GetCurrentProcessId());
}

// The native host must have been launched by a browser: walk up to 8 ancestor
// levels looking for one of the Chromium-family browser images.
inline DWORD BrowserAncestorPid(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    std::vector<PROCESSENTRY32W> processes;
    PROCESSENTRY32W entry = {sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.push_back(entry);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    DWORD current = pid;
    for (int depth = 0; depth < 8; ++depth) {
        const auto found = std::ranges::find(processes, current, &PROCESSENTRY32W::th32ProcessID);
        if (found == processes.end()) break;
        current = found->th32ParentProcessID;
        const auto parent = std::ranges::find(processes, current, &PROCESSENTRY32W::th32ProcessID);
        if (parent == processes.end()) break;
        if (browser_target::IsChromiumBrowserImage(browser_target::LowerAscii(parent->szExeFile))) {
            return current;
        }
    }
    return 0;
}

inline bool HasBrowserAncestor(DWORD pid) { return BrowserAncestorPid(pid) != 0; }

// ---------------------------------------------------------------------------
// Window identity: the extension reports its window bounds in Chrome's
// device-independent pixels; the desktop compares them against the window it
// anchored at recording start. A mismatch is NOT an error to be worked around:
// it means we cannot prove the answer belongs to the window the user was
// dictating into, so the fail-closed answer is "unknown".
// ---------------------------------------------------------------------------

struct WindowBounds {
    long long left = 0;
    long long top = 0;
    long long width = 0;
    long long height = 0;
};

// Pure geometry, so it can be regression-tested offline. Chrome advertises its
// window bounds in device-independent pixels; this process is PerMonitorV2 and
// therefore observes physical pixels, so the advertised rectangle has to be
// scaled by the window's DPI before the two can be compared.
//
// NOTE: the earlier implementation tried to obtain DIP coordinates by switching
// the thread to DPI_AWARENESS_CONTEXT_UNAWARE. That does not virtualize a
// PerMonitorV2 process, so on a 125% display every window was compared as
// physical-vs-DIP and never matched ("no_window_match advertised=[0,0,1540x1672]
// anchor=[0,0,1925x2089]" - a ratio of exactly 1.25). Doing the conversion
// explicitly is both correct and testable.
inline bool BoundsMatchPhysical(const WindowBounds& advertisedDip, const RECT& physical, double scale,
                                long long toleranceDip) {
    if (advertisedDip.width <= 0 || advertisedDip.height <= 0 ||
        !std::isfinite(scale) || scale < 0.25 || scale > 16.0 || toleranceDip < 0) return false;
    const double factor = scale;
    // The tolerance may afford to be generous: `lastFocused` already narrows the
    // candidates to one window per browser instance, so this check only has to
    // reject a *different* window, which never sits within a few DIP of the same
    // origin and size. The margin also absorbs the invisible resize border a
    // maximized window carries in GetWindowRect.
    const long long tolerance = static_cast<long long>(std::lround(toleranceDip * factor));
    // Not named `near`: windef.h still defines that as an empty macro for legacy
    // 16-bit compatibility, which turns any such declaration into a syntax error.
    const auto withinTolerance = [tolerance](long long a, long long b) {
        return std::llabs(a - b) <= tolerance;
    };
    return withinTolerance(static_cast<long long>(std::lround(advertisedDip.left * factor)), physical.left) &&
           withinTolerance(static_cast<long long>(std::lround(advertisedDip.top * factor)), physical.top) &&
           withinTolerance(static_cast<long long>(std::lround(advertisedDip.width * factor)),
                           physical.right - physical.left) &&
           withinTolerance(static_cast<long long>(std::lround(advertisedDip.height * factor)),
                           physical.bottom - physical.top);
}

// Use the browser's actual UI scale (page DPR / tab zoom), independently of
// Windows DPI. Origins are relative to the monitor work area, not global zero.
inline bool BoundsMatchMonitor(WindowBounds bounds, RECT physical, double browserScale,
                               long long screenLeft, long long screenTop, const RECT& monitorWork) {
    constexpr long long limit = 10000000;
    for (const auto value : {bounds.left, bounds.top, bounds.width, bounds.height, screenLeft, screenTop}) {
        if (value < -limit || value > limit) return false;
    }
    bounds.left -= screenLeft;
    bounds.top -= screenTop;
    physical.left -= monitorWork.left;
    physical.right -= monitorWork.left;
    physical.top -= monitorWork.top;
    physical.bottom -= monitorWork.top;
    return BoundsMatchPhysical(bounds, physical, browserScale, 16);
}

// Effective DPI scale of the window (1.25 on a 125% display). GetDpiForWindow is
// the authoritative source for a per-monitor-aware window; 1.0 is the fallback.
inline double DpiScaleForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && hwnd) {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0) return static_cast<double>(dpi) / 96.0;
    }
    return 1.0;
}

inline bool BoundsMatchWindow(HWND hwnd, const WindowBounds& advertisedDip, int toleranceDip) {
    if (!hwnd || advertisedDip.width <= 0 || advertisedDip.height <= 0) return false;
    if (!IsWindow(hwnd)) return false;
    RECT physical = {};
    if (!GetWindowRect(hwnd, &physical)) return false;
    return BoundsMatchPhysical(advertisedDip, physical, DpiScaleForWindow(hwnd), toleranceDip);
}

inline bool ReplyMatchesWindow(const std::string& reply, HWND anchor) {
    constexpr long long missing = 10000001;
    const WindowBounds bounds{detail::JsonInt(reply, "windowLeft", missing),
                              detail::JsonInt(reply, "windowTop", missing),
                              detail::JsonInt(reply, "windowWidth", missing),
                              detail::JsonInt(reply, "windowHeight", missing)};
    const long long scaleMicros = detail::JsonInt(reply, "scaleMicros", 0);
    RECT physical{};
    MONITORINFO monitor{sizeof(monitor)};
    if (!IsWindow(anchor) || !GetWindowRect(anchor, &physical) ||
        !GetMonitorInfoW(MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    return BoundsMatchMonitor(bounds, physical, static_cast<double>(scaleMicros) / 1000000.0,
                              detail::JsonInt(reply, "screenLeft", missing),
                              detail::JsonInt(reply, "screenTop", missing), monitor.rcWork);
}

// ---------------------------------------------------------------------------
// Fail-closed answer classification. Pure, so the privacy contract can be
// regression-tested offline: only a fully corroborated "positively identified
// editable control that is not a password field, focused, identity matched"
// answer may become Safe.
// ---------------------------------------------------------------------------

enum class FocusState {
    Sensitive,
    Safe,
    UnknownNotConnected,
    UnknownNoWindowMatch,
    UnknownWindowAmbiguous,
    UnknownQuery,
    UnknownFocus,
    UnknownIdentity,
    UnknownNotTextInput,
    UnknownTimeout,
};

inline FocusState ClassifyFocus(bool passwordHit, bool passwordKnown, bool hasFocusInFrame,
                                bool identityMatched, bool textInputIdentified) {
    if (passwordHit) return FocusState::Sensitive;
    if (!passwordKnown) return FocusState::UnknownQuery;
    if (!hasFocusInFrame) return FocusState::UnknownFocus;
    if (!identityMatched) return FocusState::UnknownIdentity;
    // "It does not look like a password" is only an observation when we actually
    // reached the focused control. A document behind a shadow boundary retargets
    // document.activeElement to its host, and a CLOSED shadow root is
    // indistinguishable from a plain non-editable element from the outside, so a
    // <my-password-field> wrapper would otherwise be reported as "not a password"
    // and let the transcript into the shared history. Requiring a positively
    // identified editable control keeps that case fail-closed; the cost is that
    // dictating with focus on a plain element (body, a button) no longer records
    // history, which is a bounded loss and the safe direction.
    if (!textInputIdentified) return FocusState::UnknownNotTextInput;
    return FocusState::Safe;
}

// Maps an unsuccessful query onto the same state space, so callers have a single
// classification path whether the answer arrived or not.
inline FocusState FocusStateFromFailure(const std::string& reason) {
    if (reason == kReasonNotConnected) return FocusState::UnknownNotConnected;
    if (reason == kReasonNoWindowMatch) return FocusState::UnknownNoWindowMatch;
    if (reason == kReasonWindowAmbiguous) return FocusState::UnknownWindowAmbiguous;
    if (reason == kReasonTimeout) return FocusState::UnknownTimeout;
    return FocusState::UnknownQuery;
}

inline const char* FocusStateName(FocusState state) {
    switch (state) {
        case FocusState::Sensitive: return "sensitive";
        case FocusState::Safe: return "safe";
        case FocusState::UnknownNotConnected: return "unknown_not_connected";
        case FocusState::UnknownNoWindowMatch: return "unknown_no_window_match";
        case FocusState::UnknownWindowAmbiguous: return "unknown_window_ambiguous";
        case FocusState::UnknownNotTextInput: return "unknown_not_text_input";
        case FocusState::UnknownQuery: return "unknown_query";
        case FocusState::UnknownFocus: return "unknown_focus";
        case FocusState::UnknownIdentity: return "unknown_identity";
        case FocusState::UnknownTimeout: return "unknown_timeout";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Message helpers
// ---------------------------------------------------------------------------

inline std::string BuildQuery(long long id, Need need, int windowId, size_t valueCharacters) {
    return std::format(
        "{{\"version\":{},\"type\":\"{}\",\"id\":{},\"need\":\"{}\",\"windowId\":{},"
        "\"valueChars\":{}}}",
        kProtocolVersion, kTypeQuery, id, need == Need::Probe ? "probe" : "field", windowId,
        static_cast<long long>(valueCharacters));
}

struct HelloWindow {
    int id = -1;
    WindowBounds bounds;
    bool lastFocused = false;
    bool focused = false;
};

struct HelloInfo {
    std::string extensionId;
    std::vector<HelloWindow> windows;
};

// Reads the fields of a `hello` message. Returns nullopt when required fields are
// missing or out of range; the caller then drops the peer.
inline std::optional<HelloInfo> ParseHello(const std::string& json) {
    if (ExtractJsonStringDecoded(json, "type") != L"hello") return std::nullopt;
    HelloInfo info;
    info.extensionId = WideToUtf8(ExtractJsonStringDecoded(json, "ext"));
    if (info.extensionId.empty() || info.extensionId.size() > 64) return std::nullopt;
    const std::string key = "\"windows\"";
    const size_t arrayAt = json.find(key);
    if (arrayAt == std::string::npos) return std::nullopt;
    size_t pos = json.find('[', arrayAt);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    // Window objects are small and flat; walking them by hand keeps this module
    // free of a JSON dependency while still bounding every field.
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' ||
                                     json[pos] == '\r' || json[pos] == '\t')) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') return std::nullopt;
        const size_t end = json.find('}', pos);
        if (end == std::string::npos) return std::nullopt;
        const std::string object = json.substr(pos, end - pos + 1);
        HelloWindow window;
        const auto windowId = detail::JsonInt(object, "id", -1);
        if (windowId < 0 || windowId > 0x7FFFFFFF) return std::nullopt;
        window.id = static_cast<int>(windowId);
        window.bounds.left = detail::JsonInt(object, "left", 0);
        window.bounds.top = detail::JsonInt(object, "top", 0);
        window.bounds.width = detail::JsonInt(object, "width", 0);
        window.bounds.height = detail::JsonInt(object, "height", 0);
        window.lastFocused = detail::JsonBool(object, "lastFocused").value_or(false);
        window.focused = detail::JsonBool(object, "focused").value_or(false);
        if (window.id >= 0 && window.bounds.width > 0 && window.bounds.height > 0) {
            if (info.windows.size() >= kMaxWindows) return std::nullopt;
            info.windows.push_back(window);
        }
        pos = end + 1;
    }
    return info;
}

// One decrypted field lookup. `ok == false` always means "no context"; callers
// must not substitute an accessibility read.
struct QueryResult {
    bool ok = false;
    std::string reason = kReasonNotConnected;
    // Compact evidence for the diagnostic log only: peer count, the window
    // rectangle(s) the companion advertised and the rectangle we anchored. Never
    // shown to the user, never used for a decision. Keeping it here is what makes
    // "connected but the rectangles disagree" distinguishable from "no companion"
    // without a debugger attached.
    std::string diagnostics;
    bool hasFocus = false;
    bool isPassword = false;
    bool isTextInput = false;
    bool identityMatched = false;
    std::wstring value;
};

// ---------------------------------------------------------------------------
// Desktop-side client
// ---------------------------------------------------------------------------

class Bridge {
public:
    static Bridge& Instance() {
        static Bridge instance;
        return instance;
    }

    // Starts the accept loop. Idempotent; safe to call from the composition root
    // before any recording can happen.
    void Start() {
        if (state_->accept.joinable()) return;
        const PipeIdentity identity = CurrentIdentity();
        if (identity.name.empty()) return;
        if (state_->ready) CloseHandle(state_->ready);
        state_->ready = CreateReadyEvent(identity);
        if (!state_->ready) return;
        ResetEvent(state_->ready);
        ResetEvent(state_->stop);
        state_->accept = std::jthread([this, identity](std::stop_token stop) { AcceptLoop(identity, stop); });
    }

    void Stop() {
        state_->accept.request_stop();
        SetEvent(state_->stop);
        if (state_->accept.joinable()) state_->accept.join();
        std::vector<std::shared_ptr<Peer>> peers;
        {
            std::lock_guard lock(state_->mutex);
            peers.swap(state_->peers);
        }
        for (const auto& peer : peers) {
            peer->thread.request_stop();
            // Closing the connection also rejects a read started after cancellation.
            DisconnectNamedPipe(peer->pipe);
            CancelIoEx(peer->pipe, nullptr);
        }
        for (const auto& peer : peers) {
            if (peer->thread.joinable()) peer->thread.join();
        }
    }

    // Bounded request/response. `anchor` is the top-level window captured when
    // recording started; the answer is only accepted for a window whose bounds
    // match it.
    //
    // The sensitive probe and the focused-field read run on two independent
    // workers and can overlap, so replies are correlated by request id rather
    // than kept in a single slot: one shared slot would let either waiter consume
    // the other's answer and report a bogus failure.
    QueryResult Query(HWND anchor, Need need, size_t valueCharacters, DWORD deadlineMs) {
        QueryResult result;
        if (!anchor) {
            result.reason = kReasonNoAnchor;
            return result;
        }
        const PeerMatch match = PickPeer(anchor);
        result.diagnostics = match.diagnostics;
        if (!match.peer) {
            result.reason = match.failure ? match.failure : kReasonNotConnected;
            return result;
        }
        const std::shared_ptr<Peer>& target = match.peer;
        const long long id = ++state_->requestSeq;
        const std::string command = BuildQuery(id, need, match.windowId, valueCharacters);
        {
            std::lock_guard lock(target->mutex);
            target->replies.erase(id);
        }
        {
            std::lock_guard writeLock(target->write);
            if (!WriteFrame(target->pipe, command, 50, detail::PipeIo::Overlapped)) {
                result.reason = kReasonDisconnected;
                return result;
            }
        }
        std::string reply;
        {
            std::unique_lock lock(target->mutex);
            const bool received = target->cv.wait_for(lock, std::chrono::milliseconds(deadlineMs), [&] {
                return !target->alive || target->replies.count(id) != 0;
            });
            if (!target->alive) {
                result.reason = kReasonDisconnected;
                return result;
            }
            const auto found = target->replies.find(id);
            if (!received || found == target->replies.end()) {
                result.reason = kReasonTimeout;
                return result;
            }
            reply = found->second;
            target->replies.erase(found);
        }
        // The id was already used to select the entry; this is a second check so a
        // malformed frame can never be attributed to the wrong request.
        if (detail::JsonInt(reply, "id", -1) != id) {
            result.reason = kReasonInvalidMessage;
            return result;
        }
        if (!detail::JsonBool(reply, "ok").value_or(false)) {
            const std::wstring reason = ExtractJsonStringDecoded(reply, "reason");
            result.reason = reason.empty() ? std::string(kReasonTargetUnavailable) : WideToUtf8(reason);
            return result;
        }
        if (detail::JsonInt(reply, "windowId", -1) != match.windowId) {
            result.reason = kReasonInvalidMessage;
            return result;
        }
        if (!detail::JsonBool(reply, "focus").value_or(false)) {
            result.reason = "no_focus";
            return result;
        }
        if (!ReplyMatchesWindow(reply, anchor)) {
            result.reason = kReasonNoWindowMatch;
            return result;
        }
        const auto password = detail::JsonBool(reply, "password");
        if (!password.has_value()) {
            result.reason = kReasonInvalidMessage;
            return result;
        }
        result.identityMatched = true;
        result.ok = true;
        result.hasFocus = true;
        result.isPassword = *password;
        result.isTextInput = detail::JsonBool(reply, "textInput").value_or(false);
        // A password answer never carries a value; the extension does not send one
        // and this branch guarantees the desktop never looks for one.
        if (need == Need::Field && !result.isPassword) {
            result.value = ExtractJsonStringDecoded(reply, "value");
        }
        return result;
    }

private:
    struct Peer {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        DWORD browserPid = 0;
        int windowId = -1;
        bool alive = true;
        std::mutex mutex;
        std::mutex write;
        std::condition_variable cv;
        std::optional<HelloInfo> hello;
        std::unordered_map<long long, std::string> replies;
        std::jthread thread;
        ~Peer() {
            if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        }
    };

    struct State {
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<Peer>> peers;
        std::jthread accept;
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE ready = nullptr;
        std::atomic<long long> requestSeq{0};
        ~State() {
            CloseHandle(stop);
            if (ready) CloseHandle(ready);
        }
    };

    Bridge() : state_(std::make_unique<State>()) {}
    ~Bridge() { Stop(); }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    void AcceptLoop(const PipeIdentity& identity, std::stop_token stop) {
        // Every pipe instance must carry the owner-only DACL, so the descriptor
        // stays alive for the whole accept loop.
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(identity.sddl.c_str(), SDDL_REVISION_1,
                                                                  &descriptor, nullptr)) {
            return;
        }
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
        while (!stop.stop_requested()) {
            std::vector<std::shared_ptr<Peer>> retired;
            {
                std::lock_guard lock(state_->mutex);
                for (auto it = state_->peers.begin(); it != state_->peers.end();) {
                    bool alive = true;
                    {
                        std::lock_guard peerLock((*it)->mutex);
                        alive = (*it)->alive;
                    }
                    if (!alive) {
                        retired.push_back(*it);
                        it = state_->peers.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            for (const auto& old : retired) {
                if (old->thread.joinable()) old->thread.join();
            }
            HANDLE pipe = CreateNamedPipeW(identity.name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                               PIPE_REJECT_REMOTE_CLIENTS,
                                           PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &security);
            if (pipe == INVALID_HANDLE_VALUE) break;
            SetEvent(state_->ready);
            OVERLAPPED ov = {};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            BOOL connected = ConnectNamedPipe(pipe, &ov);
            const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_IO_PENDING) {
                HANDLE events[] = {ov.hEvent, state_->stop};
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) {
                    DWORD count = 0;
                    connected = GetOverlappedResult(pipe, &ov, &count, FALSE);
                } else {
                    CancelIoEx(pipe, &ov);
                    DWORD count = 0;
                    GetOverlappedResult(pipe, &ov, &count, TRUE);
                }
            } else if (error == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
            CloseHandle(ov.hEvent);
            if (!connected || stop.stop_requested()) {
                CloseHandle(pipe);
                continue;
            }
            ULONG pid = 0;
            const DWORD browserPid = GetNamedPipeClientProcessId(pipe, &pid) ? BrowserAncestorPid(pid) : 0;
            if (!browserPid || !SameSession(pid) || !SameImageAsSelf(pid)) {
                CloseHandle(pipe);
                continue;
            }
            auto peer = std::make_shared<Peer>();
            peer->pipe = pipe;
            peer->browserPid = browserPid;
            {
                std::lock_guard lock(state_->mutex);
                if (state_->peers.size() >= kMaxPeers) {
                    CloseHandle(pipe);
                    peer->pipe = INVALID_HANDLE_VALUE;
                    continue;
                }
                state_->peers.push_back(peer);
            }
            peer->thread = std::jthread([peer](std::stop_token token) { PeerLoop(peer, token); });
        }
        if (descriptor) LocalFree(descriptor);
        ResetEvent(state_->ready);
    }

    static void PeerLoop(const std::shared_ptr<Peer>& peer, std::stop_token stop) {
        while (!stop.stop_requested()) {
            auto frame = detail::ReadFrame(peer->pipe, INFINITE, detail::PipeIo::Overlapped);
            if (!frame) break;
            const std::wstring type = ExtractJsonStringDecoded(*frame, "type");
            std::lock_guard lock(peer->mutex);
            if (type == L"hello") {
                const auto hello = ParseHello(*frame);
                if (!hello) break;
                // The manifest already pins allowed_origins; recording the id keeps
                // "which extension is talking" diagnosable, and a future rotation
                // can be spotted here instead of guessed at.
                peer->hello = std::move(*hello);
            } else if (type == L"result" || type == L"unavailable") {
                const long long id = detail::JsonInt(*frame, "id", -1);
                if (id >= 0) {
                    // Bounded: a reply nobody is waiting for must not accumulate.
                    if (peer->replies.size() >= 8) peer->replies.clear();
                    peer->replies[id] = *frame;
                }
            } else {
                break;
            }
            peer->cv.notify_all();
        }
        std::lock_guard lock(peer->mutex);
        peer->alive = false;
        peer->cv.notify_all();
    }

    // Resolution outcome for an anchored window. `failure` distinguishes "no
    // companion at all" from "companion present but its windows do not line up",
    // and `diagnostics` carries the rectangles that decided it - the difference
    // between those two used to be invisible in the logs.
    struct PeerMatch {
        std::shared_ptr<Peer> peer;
        int windowId = -1;
        const char* failure = kReasonNotConnected;
        std::string diagnostics;
    };

    // Route only to the anchor's native browser process and its last-focused
    // window. The on-demand reply supplies current geometry and the actual
    // browser scale; it must match before any context/privacy answer is accepted.
    //
    // Returns the peer together with the matched window id, so the caller never
    // reads window state outside the lock that established it.
    PeerMatch PickPeer(HWND anchor) const {
        PeerMatch match;
        DWORD anchorPid = 0;
        GetWindowThreadProcessId(anchor, &anchorPid);
        size_t peerCount = 0;
        std::string advertised;
        std::vector<std::pair<std::shared_ptr<Peer>, int>> matches;
        std::vector<std::pair<std::shared_ptr<Peer>, int>> focusedMatches;
        {
            std::lock_guard lock(state_->mutex);
            for (const auto& peer : state_->peers) {
                std::lock_guard peerLock(peer->mutex);
                if (!peer->alive || !peer->hello) continue;
                ++peerCount;
                if (!anchorPid || peer->browserPid != anchorPid) continue;
                for (const auto& window : peer->hello->windows) {
                    if (!window.lastFocused) continue;
                    if (advertised.size() < 96) {
                        advertised += std::format("[{},{},{}x{}]", window.bounds.left, window.bounds.top,
                                                  window.bounds.width, window.bounds.height);
                    }
                    matches.emplace_back(peer, window.id);
                    if (window.focused) focusedMatches.emplace_back(peer, window.id);
                }
            }
        }
        // Chrome profiles can share a browser PID; only the actually focused
        // profile wins over retained last-focused windows in other profiles.
        if (!focusedMatches.empty()) matches = std::move(focusedMatches);
        // The anchor is a real HWND captured at recording start; this is the same
        // rectangle the match was computed against.
        RECT anchorRect{};
        GetWindowRect(anchor, &anchorRect);
        match.diagnostics = std::format("peers={} matched={} dpi={:.2f} advertised={} anchor=[{},{},{}x{}]",
                                        peerCount, matches.size(), DpiScaleForWindow(anchor),
                                        advertised.empty() ? "none" : advertised,
                                        anchorRect.left, anchorRect.top,
                                        anchorRect.right - anchorRect.left,
                                        anchorRect.bottom - anchorRect.top);
        if (matches.empty()) {
            match.failure = peerCount == 0 ? kReasonNotConnected : kReasonNoWindowMatch;
            return match;
        }
        if (matches.size() > 1) {
            match.failure = kReasonWindowAmbiguous;
            return match;
        }
        auto& chosen = matches.front();
        {
            std::lock_guard peerLock(chosen.first->mutex);
            if (!chosen.first->alive) {
                match.failure = kReasonDisconnected;
                return match;
            }
            chosen.first->windowId = chosen.second;
        }
        match.peer = chosen.first;
        match.windowId = chosen.second;
        match.failure = nullptr;
        return match;
    }

    std::unique_ptr<State> state_;
};

// ---------------------------------------------------------------------------
// Native Messaging host mode
// ---------------------------------------------------------------------------

inline bool IsNativeHostInvocation() {
    const std::wstring commandLine = GetCommandLineW();
    if (commandLine.find(L"chrome-extension://") == std::wstring::npos) return false;
    return GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE &&
           GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_PIPE;
}

// Restricted forwarding loop: it validates frames and relays them, and creates
// no window, no tray icon, no model and no accessibility client.
inline int RunNativeHost() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const PipeIdentity identity = CurrentIdentity();
    if (identity.name.empty() || !HasBrowserAncestor(GetCurrentProcessId())) return 2;

    auto hello = detail::ReadFrame(input, 10000, detail::PipeIo::Synchronous);
    if (!hello || ExtractJsonStringDecoded(*hello, "type") != L"hello") return 2;
    // Defence in depth on top of the manifest's allowed_origins: only the pinned
    // companion may talk to the desktop side. The extension id is fixed by the
    // `key` in its manifest, so this holds for a store build as well.
    const auto helloInfo = ParseHello(*hello);
    if (!helloInfo || helloInfo->extensionId != kExtensionIdUtf8) return 2;

    std::atomic<bool> ended = false;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::vector<std::string> pending{*hello};
    // Shared with the reader thread so a closed stdio channel can unblock the
    // main loop, which otherwise sits in an unbounded pipe read: without this the
    // host would only exit when the browser or the desktop side closed the other
    // end for us.
    std::atomic<HANDLE> pipeHandle{INVALID_HANDLE_VALUE};
    HANDLE endedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE readyEvent = CreateReadyEvent(identity);
    if (!endedEvent || !readyEvent) {
        if (endedEvent) CloseHandle(endedEvent);
        if (readyEvent) CloseHandle(readyEvent);
        return 2;
    }
    std::jthread reader([&] {
        while (!ended) {
            auto frame = detail::ReadFrame(input, INFINITE, detail::PipeIo::Synchronous);
            if (!frame) break;
            std::lock_guard lock(queueMutex);
            if (pending.size() >= 64) break;
            pending.push_back(std::move(*frame));
            cv.notify_one();
        }
        ended = true;
        SetEvent(endedEvent);
        cv.notify_one();
        if (const HANDLE pipe = pipeHandle.load(); pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe, nullptr);
        }
    });
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (!ended) {
        HANDLE events[] = {readyEvent, endedEvent};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0) break;
        pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(identity.name.c_str(), 1000);
        } else {
            ResetEvent(readyEvent);
            // Retry once after a reset so a listener created between the failed
            // open and the reset cannot lose its readiness notification.
            pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) break;
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        pipeHandle.store(pipe);
        ULONG server = 0;
        if (!GetNamedPipeServerProcessId(pipe, &server) || !SameSession(server) || !SameImageAsSelf(server)) {
            ended = true;
        }
        std::jthread writer([&] {
            while (!ended) {
                std::vector<std::string> frames;
                {
                    std::unique_lock lock(queueMutex);
                    cv.wait(lock, [&] { return ended.load() || !pending.empty(); });
                    frames.swap(pending);
                }
                for (const auto& frame : frames) {
                    if (!detail::WriteFrame(pipe, frame, 1000, detail::PipeIo::Overlapped)) {
                        ended = true;
                        SetEvent(endedEvent);
                        break;
                    }
                }
            }
        });
        while (!ended) {
            auto frame = detail::ReadFrame(pipe, INFINITE, detail::PipeIo::Overlapped, endedEvent);
            if (!frame || !detail::WriteFrame(output, *frame, 1000, detail::PipeIo::Synchronous)) break;
        }
        ended = true;
        cv.notify_one();
        CancelIoEx(pipe, nullptr);
        writer.join();
    }
    ended = true;
    cv.notify_one();
    while (WaitForSingleObject(reader.native_handle(), 5) == WAIT_TIMEOUT) {
        CancelSynchronousIo(reader.native_handle());
    }
    reader.join();
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    CloseHandle(endedEvent);
    CloseHandle(readyEvent);
    return 0;
}

// Entry point for the composition root. MUST be the first statement of wWinMain,
// before the single-instance mutex: a browser launches this executable as its
// native messaging host while the desktop application is normally already
// running, and the mutex check below would otherwise turn the host into "VoxType
// is already running" and kill it.
//
// Returns the process exit code when this invocation WAS the native messaging
// host (0 = served, 2 = refused: not launched by a browser, or not our
// companion), and nullopt to continue starting the desktop application. The
// desktop listener starts only after acquiring the single-instance mutex, so a
// rejected duplicate launch cannot steal its pipe or reset its readiness event.
// The exit code is kept because it is the only
// externally visible evidence of *why* a host invocation did nothing.
inline std::optional<int> RunNativeHostIfInvoked() {
    if (IsNativeHostInvocation()) return RunNativeHost();
    return std::nullopt;
}

}  // namespace browser_bridge
