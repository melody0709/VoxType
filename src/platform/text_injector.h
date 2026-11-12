#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "selection_context.h"

namespace platform {

enum class InjectionFailureReason {
    None,
    NoForegroundWindow,        // fg == nullptr || !IsWindow(fg) || IsIconic(fg)
    BlockedByUipi,             // target process integrity > current process integrity && !UIAccess
    NonEditableShellSurface,   // desktop / taskbar / Alt+Tab / lock screen / Explorer file list / VoxType HUD
    DisabledOrReadOnlyControl, // !IsWindowEnabled(...) || (Edit/RichEdit && ES_READONLY)
    WindowSwitchedAfterStop,   // t2 (stop) -> t3 (deliver) top-level foreground window changed or destroyed
    SelectionRewriteSkipped,   // ReplaceSelectionTextImeAware skipped due to selection/focus change
    DeliveryFailed,            // SendInput / PostMessageW / PasteViaClipboard failed
};

const char* InjectionFailureReasonName(InjectionFailureReason reason);

struct InjectionOutcome {
    bool delivered = false;
    bool copiedToClipboard = false;
    bool clipboardCopyPending = false;
    bool skippedDueToPassword = false;
    InjectionFailureReason reason = InjectionFailureReason::None;
    std::wstring detail;
};

// Per-injection metadata handed in by the caller (app layer). It feeds the
// injection ledger, the learning reflection worker and the later undo/organize
// actions. Passing nullptr keeps the historical behaviour: no ledger entry and
// no reflection.
struct InjectionContext {
    std::wstring sourceText;              // transcript that produced this injection
    bool sensitive = false;               // fail-closed focus probe ("not safe") for learning_intake
    bool confirmedSensitiveAtStart = false; // explicit password hit at t0 (NOT fail-closed)
    bool llmApplied = false;              // text came back from the LLM refine path
    bool capturePreSelection = false;     // snapshot the selection before typing
    DWORD reflectionDelayMs = 0;          // 0 disables the read-back worker
    HWND stopRootWindow = nullptr;        // t2 top-level foreground window anchor
    DWORD stopRootProcessId = 0;          // t2 process ID to guard against HWND reuse
    bool copyToClipboardOnFailure = true;
    bool fallbackOnWindowSwitch = true;
};

bool SendUnicodeText(const std::wstring& text, HWND expectedForeground = nullptr);
bool PasteTextImeAware(const std::wstring& text,
                       bool forceUnicodeInput = false,
                       const InjectionContext* context = nullptr);
InjectionOutcome PasteTextImeAwareWithOutcome(const std::wstring& text,
                                              bool forceUnicodeInput = false,
                                              const InjectionContext* context = nullptr);
bool ReplaceSelectionTextImeAware(const SelectionContext& selection,
                                  const std::wstring& text,
                                  std::wstring* error = nullptr);
InjectionOutcome ReplaceSelectionTextImeAwareWithOutcome(const SelectionContext& selection,
                                                         const std::wstring& text,
                                                         const InjectionContext* context = nullptr);
enum class ClipboardCopyStatus { Failed, Pending, Copied };

ClipboardCopyStatus CopyTextToClipboardFallback(const std::wstring& text,
                                 bool excludeFromHistory,
                                 bool confirmedSensitiveAtStart = false,
                                 bool* skippedDueToPassword = nullptr,
                                 bool checkCurrentFocusPassword = true);
bool IsCurrentFocusWin32Password();
void InvalidatePendingClipboardRestore();
bool IsCapsLockOn();
void SendCapsLockTap();
void RestoreCapsLockState(bool wasOn);
void OnClipboardRestoreTimer();
void CleanupClipboardOnExit();

}  // namespace platform

using platform::InjectionFailureReason;
using platform::InjectionFailureReasonName;
using platform::InjectionOutcome;
using platform::ClipboardCopyStatus;
using platform::SendUnicodeText;
using platform::PasteTextImeAware;
using platform::PasteTextImeAwareWithOutcome;
using platform::InjectionContext;
using platform::ReplaceSelectionTextImeAware;
using platform::ReplaceSelectionTextImeAwareWithOutcome;
using platform::CopyTextToClipboardFallback;
using platform::IsCurrentFocusWin32Password;
using platform::InvalidatePendingClipboardRestore;
using platform::IsCapsLockOn;
using platform::SendCapsLockTap;
using platform::RestoreCapsLockState;
using platform::OnClipboardRestoreTimer;
using platform::CleanupClipboardOnExit;
