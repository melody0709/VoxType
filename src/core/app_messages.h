#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

constexpr UINT kCapsLockLongPressMs = 300;
// Key-up does not stop capture immediately; the short delay keeps the tail of
// the utterance inside the capture window and lets a quick re-press continue
// the same recording.
constexpr UINT kRecordingStopDelayMs = 150;
// After a recording stops (or a pending capture is discarded), the microphone
// device stays open this long so back-to-back recordings and CapsLock taps do
// not pay the device open cost again.
constexpr UINT kMicKeepAliveMs = 2500;

constexpr UINT ID_TRAY_VERSION = 1001;
constexpr UINT ID_TRAY_SETTINGS = 1002;
constexpr UINT ID_TRAY_RELOAD = 1003;
constexpr UINT ID_TRAY_QUIT = 1004;
constexpr UINT ID_TRAY_DEBUG_MODE = 1005;
constexpr UINT ID_TRAY_FORCE_UNICODE = 1006;
