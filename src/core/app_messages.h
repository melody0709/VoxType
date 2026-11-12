#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

constexpr wchar_t kAppName[] = L"VoxType";
constexpr wchar_t kMainClass[] = L"VoxType.Main";
constexpr wchar_t kSettingsClass[] = L"VoxType.Settings";
constexpr wchar_t kHudClass[] = L"VoxType.Hud";
constexpr wchar_t kHotkeyEditClass[] = L"VoxType.HotkeyEdit";

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kReloadMessage = WM_APP + 2;
constexpr UINT kAsrResultMessage = WM_APP + 3;
constexpr UINT kLlmResultMessage = WM_APP + 4;
constexpr UINT kPreloadDoneMessage = WM_APP + 5;
constexpr UINT kHudUpdateMessage = WM_APP + 6;
constexpr UINT kDoubaoImeCredentialsMessage = WM_APP + 7;
constexpr UINT kDoubaoImeSettingsRefreshMessage = WM_APP + 8;
constexpr UINT kHudUpdateWithOptionsMessage = WM_APP + 9;
// Main-window only. Settings uses WM_APP + 10 locally for test results.
constexpr UINT kAsrAttemptFinalMessage = WM_APP + 10;
constexpr UINT kHotkeyRecordingMessage = WM_APP + 11;
// Posted by the capture worker when the microphone/driver fails after a
// recording has already started.  The generation in wParam invalidates stale
// messages from a previous recording; lParam carries the native error code.
constexpr UINT kAudioCaptureErrorMessage = WM_APP + 12;
constexpr UINT kWaveInCaptureErrorMessage = WM_APP + 13;
constexpr UINT kDoubaoImeSettingsCredentialsMessage = WM_APP + 14;
// Privacy-safe clipboard diagnostics; wParam is the event, lParam is a count
// (restore failures) or elapsed milliseconds (pending paste).
constexpr UINT kClipboardDiagnosticMessage = WM_APP + 15;
constexpr WPARAM kClipboardRestoreFailed = 1;
constexpr WPARAM kClipboardPasteStillPending = 2;
constexpr WPARAM kClipboardFallbackCopied = 3;
constexpr WPARAM kClipboardFallbackCancelled = 4;
// Posted by the injection reflection worker once it has read the focused
// document back (see learning_intake). lParam owns a heap InjectionReflection;
// the handler deletes it. The sequence lets a stale read-back be dropped, and
// `reason` carries a static skip reason so the app layer owns all logging.
constexpr UINT kInjectionReflectionMessage = WM_APP + 16;
// Settings 窗口在语言切换保存后自我重建（destroy + create），控件全部换新语言表。
constexpr UINT kUiLanguageReapplyMessage = WM_APP + 17;
// Path service 的模型下载完成通知（lParam 持有 heap std::wstring 目录或为空）。
// **发往主窗口**，因为 Settings 窗口会被语言切换销毁重建；主窗口再转给当前 Settings 窗口。
// payload 的所有权随消息转移：无人接收时必须由接收方 delete。
constexpr UINT kModelDownloadDoneMessage = WM_APP + 20;

// 同线程跨窗口转发携带 heap payload 的消息必须**同步**派发。
// PostMessage 的转发会排在已入队的销毁消息之后（WM_CLOSE、语言重建的 DestroyWindow、
// 进程退出的 DestroyWindow）；目标窗口一旦被销毁，系统会把队列里发往它的消息连同
// payload 一起静默丢弃——而 PostMessage 当时仍返回 TRUE，调用方以为已经交出去了。
// 返回 false 表示目标窗口已不在，调用方必须自行回收 lParam。
inline bool SendToWindow(HWND target, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!target || !IsWindow(target)) return false;
    SendMessageW(target, msg, wParam, lParam);
    return true;
}

struct InjectionReflection {
    uint64_t sequence = 0;
    bool readSucceeded = false;
    const char* reason = "";
    std::wstring observedText;
};

constexpr WPARAM kHotkeyRecordingStart = 1;
constexpr WPARAM kHotkeyRecordingStop = 2;
constexpr WPARAM kHotkeyCapsLockRecordingStop = 3;
// CapsLock KEYDOWN starts capture immediately (300 ms long-press verdict is
// deferred); a short press discards the pending capture instead.
constexpr WPARAM kHotkeyCaptureBegin = 4;
constexpr WPARAM kHotkeyCaptureDiscard = 5;

constexpr UINT kTrayId = 1;
constexpr UINT_PTR kHudHideTimer = 1;
constexpr UINT_PTR kCapsLockLongPressTimer = 2;
constexpr UINT_PTR kHudAnimationTimer = 3;
constexpr UINT_PTR kStreamingWatchdogTimer = 4;
constexpr UINT_PTR kRecordingStopDelayTimer = 5;
constexpr UINT_PTR kMicKeepAliveTimer = 6;
constexpr UINT_PTR kRecordingWatchdogTimer = 7;
constexpr UINT_PTR kClipboardRestoreTimer = 8;
constexpr UINT_PTR kWtsRetryTimer = 9;

constexpr UINT kCapsLockLongPressMs = 300;
// Key-up does not stop capture immediately; the short delay keeps the tail of
// the utterance inside the capture window and lets a quick re-press continue
// the same recording.
constexpr UINT kRecordingStopDelayMs = 150;
// After a recording stops (or a pending capture is discarded), the microphone
// device stays open this long so back-to-back recordings and CapsLock taps do
// not pay the device open cost again.
constexpr UINT kMicKeepAliveMs = 2500;
constexpr UINT kRecordingWatchdogIntervalMs = 500;
constexpr ULONGLONG kMaxRecordingDurationMs = 60000;

constexpr UINT ID_TRAY_VERSION = 1001;
constexpr UINT ID_TRAY_SETTINGS = 1002;
constexpr UINT ID_TRAY_RELOAD = 1003;
constexpr UINT ID_TRAY_QUIT = 1004;
constexpr UINT ID_TRAY_DEBUG_MODE = 1005;
constexpr UINT ID_TRAY_FORCE_UNICODE = 1006;
constexpr UINT ID_TRAY_COPY_LAST_TRANSCRIPT = 1007;
