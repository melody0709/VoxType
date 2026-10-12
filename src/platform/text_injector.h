#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "selection_context.h"

namespace platform {

// Per-injection metadata handed in by the caller (app layer). It feeds the
// injection ledger, the learning reflection worker and the later undo/organize
// actions. Passing nullptr keeps the historical behaviour: no ledger entry and
// no reflection.
struct InjectionContext {
    std::wstring sourceText;         // transcript that produced this injection
    bool sensitive = false;          // focus probe said "not safe"
    bool llmApplied = false;         // text came back from the LLM refine path
    bool capturePreSelection = false;  // snapshot the selection before typing
    DWORD reflectionDelayMs = 0;     // 0 disables the read-back worker
};

bool SendUnicodeText(const std::wstring& text);
bool PasteTextImeAware(const std::wstring& text,
                       bool forceUnicodeInput = false,
                       const InjectionContext* context = nullptr);
bool ReplaceSelectionTextImeAware(const SelectionContext& selection,
                                  const std::wstring& text,
                                  std::wstring* error = nullptr);
bool IsCapsLockOn();
void SendCapsLockTap();
void RestoreCapsLockState(bool wasOn);
void OnClipboardRestoreTimer();
void CleanupClipboardOnExit();

}  // namespace platform

using platform::SendUnicodeText;
using platform::PasteTextImeAware;
using platform::InjectionContext;
using platform::ReplaceSelectionTextImeAware;
using platform::IsCapsLockOn;
using platform::SendCapsLockTap;
using platform::RestoreCapsLockState;
using platform::OnClipboardRestoreTimer;
using platform::CleanupClipboardOnExit;
