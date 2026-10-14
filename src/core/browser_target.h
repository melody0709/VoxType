#pragma once
// Browser classification for the accessibility-free browser path.
//
// WHY THIS EXISTS
// ---------------
// Chromium accumulates request modes from platform accessibility consumers in a
// PROCESS-wide mode (`EnableAXModeFromPlatform` ORs the request into
// `platform_ax_mode_`). Releasing our COM objects therefore does not close that
// mode, and the browser keeps maintaining a web accessibility tree for the rest
// of the session: exiting this application does not restore performance, only
// restarting the browser does. `WM_GETOBJECT(UiaRootObjectId)` plus
// `GetFocusedElement` / `ElementFromPoint` (see input_context.h) are exactly
// that kind of request, and this tool issues them on EVERY recording.
//
// The only reliable fix is to never make an accessibility request against a
// browser process. This module answers "is this top-level window a browser?"
// using nothing but process image name and window class name: no UIA, no MSAA,
// no COM, no window messages to the target.
//
// BOUNDARY (deliberate, documented in .plan/fix/browser-uia-accessibility-isolation-plan.md)
// ---------------------------------------------------------------------------------------
// Classification is by the TOP-LEVEL window's process image name. Electron and
// WebView2 hosts (Code.exe, Slack.exe, msedgewebview2.exe, ...) therefore stay
// on the accessibility path and keep their current focused-field behaviour. They
// still pay the Chromium accessibility cost, but calling a Chromium-based
// desktop application a "browser" would silently remove context from every such
// application, which is a larger behaviour change than this fix is scoped for.
// Extending the list is a one-line change here plus a regression check.

#include <windows.h>

#include <string>
#include <string_view>

namespace browser_target {

enum class Kind {
    NotBrowser,
    Chromium,  // Chromium-family browser: skips accessibility, may use the companion extension
    Gecko,     // Gecko-family (Firefox and friends): skips accessibility, no companion extension yet
};

struct Target {
    Kind kind = Kind::NotBrowser;
    std::wstring exeImageName;  // lower-case basename, e.g. L"chrome.exe"; empty when unknown
    std::wstring windowClass;   // lower-case class name; empty when unknown

    bool IsBrowser() const { return kind != Kind::NotBrowser; }
};

// ASCII-only lower-casing. Process image names and window class names are ASCII
// in practice; keeping it ASCII-only makes the helper pure and locale-free.
inline std::wstring LowerAscii(std::wstring_view value) {
    std::wstring lower(value);
    for (wchar_t& ch : lower) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return lower;
}

// Basename of a full image path, or the input when it already is a basename.
inline std::wstring ImageBasename(std::wstring_view imagePath) {
    const size_t slash = imagePath.find_last_of(L"\\/");
    return LowerAscii(slash == std::wstring_view::npos ? imagePath : imagePath.substr(slash + 1));
}

// Pure name lists. Chromium-family browsers drive the MV3 companion extension;
// Gecko-family ones only get the isolation (their extensions use a different
// native-messaging registration and are not shipped yet).
inline bool IsChromiumBrowserImage(std::wstring_view exeImageName) {
    static constexpr std::wstring_view kNames[] = {
        L"chrome.exe",     L"msedge.exe",      L"brave.exe",    L"vivaldi.exe",
        L"opera.exe",      L"opera_gx.exe",    L"chromium.exe", L"thorium.exe",
        L"arc.exe",        L"chromedev.exe",   L"360se.exe",    L"360chrome.exe",
        L"qqbrowser.exe",  L"sogouexplorer.exe", L"maxthon.exe", L"liebao.exe",
    };
    for (const auto& name : kNames) {
        if (exeImageName == name) return true;
    }
    return false;
}

inline bool IsGeckoBrowserImage(std::wstring_view exeImageName) {
    static constexpr std::wstring_view kNames[] = {
        L"firefox.exe", L"waterfox.exe", L"librewolf.exe", L"floorp.exe",
        L"zen.exe",     L"thunderbird.exe", L"palemoon.exe",
    };
    for (const auto& name : kNames) {
        if (exeImageName == name) return true;
    }
    return false;
}

inline bool IsBrowserImageName(std::wstring_view exeImageName) {
    const std::wstring base = ImageBasename(exeImageName);
    return IsChromiumBrowserImage(base) || IsGeckoBrowserImage(base);
}

// Pure classification of an already-resolved (image name, window class) pair.
// The window class is recorded for diagnostics but does not upgrade an unknown
// executable: `Chrome_WidgetWin_1` alone is also used by Electron and WebView2
// hosts, which must NOT be classified as browsers (see BOUNDARY above).
inline Kind Classify(std::wstring_view exeImageName, std::wstring_view windowClass) {
    (void)windowClass;
    const std::wstring base = ImageBasename(exeImageName);
    if (IsChromiumBrowserImage(base)) return Kind::Chromium;
    if (IsGeckoBrowserImage(base)) return Kind::Gecko;
    return Kind::NotBrowser;
}

// Process image basename of a live pid, or empty when it cannot be queried.
//
// QueryFullProcessImageNameW does not report the required length on failure, so
// the ordinary (<= 512 character) case uses a stack buffer and only a genuinely
// long install path pays for a heap buffer. This runs at most a handful of times
// per recording, on the probe/field workers, never from an audio or UI callback -
// there is deliberately no cross-call cache: a cache keyed by (HWND, pid) would
// have to be invalidated on HWND reuse to stay correct, and saving two
// OpenProcess calls per recording is not worth that risk.
inline std::wstring ProcessImageName(DWORD pid) {
    if (pid == 0) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t stackPath[512] = {};
    DWORD size = 512;
    std::wstring path;
    if (QueryFullProcessImageNameW(process, 0, stackPath, &size)) {
        path.assign(stackPath, size);
    } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::wstring longPath(32768, L'\0');
        size = static_cast<DWORD>(longPath.size());
        if (QueryFullProcessImageNameW(process, 0, longPath.data(), &size)) {
            longPath.resize(size);
            path = std::move(longPath);
        }
    }
    CloseHandle(process);
    return path.empty() ? std::wstring{} : ImageBasename(path);
}

// Classify the window that owns `hwnd` itself (not its top-level ancestor), which
// is what the browser probes care about: the process of the foreground window.
inline Target IdentifyWindow(HWND hwnd) {
    Target target;
    if (!hwnd) return target;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return target;
    target.exeImageName = ProcessImageName(pid);
    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256) != 0) {
        target.windowClass = LowerAscii(className);
    }
    target.kind = Classify(target.exeImageName, target.windowClass);
    return target;
}

// Walk to the root owner window and classify that. Used when a caller holds an
// arbitrary HWND (for example a focused child control) but the browser identity
// belongs to the top-level window.
inline Target IdentifyTopLevel(HWND hwnd) {
    if (!hwnd) return Target{};
    HWND root = GetAncestor(hwnd, GA_ROOTOWNER);
    if (!root) root = GetAncestor(hwnd, GA_ROOT);
    return IdentifyWindow(root ? root : hwnd);
}

inline const char* KindName(Kind kind) {
    switch (kind) {
        case Kind::Chromium: return "chromium";
        case Kind::Gecko: return "gecko";
        default: return "none";
    }
}

}  // namespace browser_target
