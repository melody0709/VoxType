#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "selection_context.h"

namespace platform {

bool SendUnicodeText(const std::wstring& text);
bool PasteTextImeAware(const std::wstring& text, bool forceUnicodeInput = false);
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
using platform::ReplaceSelectionTextImeAware;
using platform::IsCapsLockOn;
using platform::SendCapsLockTap;
using platform::RestoreCapsLockState;
using platform::OnClipboardRestoreTimer;
using platform::CleanupClipboardOnExit;
