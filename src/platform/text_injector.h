#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "selection_context.h"

namespace platform {

void SetClipboardText(const std::wstring& text);
void SendCtrlV();
void SendUnicodeText(const std::wstring& text);
void PasteTextImeAware(const std::wstring& text, bool forceUnicodeInput = false);
bool ReplaceSelectionTextImeAware(const SelectionContext& selection,
                                  const std::wstring& text,
                                  std::wstring* error = nullptr);
bool IsCapsLockOn();
void SendCapsLockTap();
void RestoreCapsLockState(bool wasOn);

}  // namespace platform

using platform::SetClipboardText;
using platform::SendCtrlV;
using platform::SendUnicodeText;
using platform::PasteTextImeAware;
using platform::ReplaceSelectionTextImeAware;
using platform::IsCapsLockOn;
using platform::SendCapsLockTap;
using platform::RestoreCapsLockState;
