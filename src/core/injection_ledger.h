#pragma once

// Single-slot ledger of the most recent text injection (P2/P6/P5b base).
//
// The ledger answers exactly one question for later consumers: "what did we just
// type, where, and is that record still valid?". Everything else (learning
// reflection, undo, manual organize) is built on top of it:
//
//   - the reflection worker validates `sequence` + foreground window before it
//     treats a read-back as the user's edit of `injectedText`;
//   - undo/organize call Consume() so a record is acted on at most once;
//   - a new recording calls Invalidate() so a stale record cannot be undone
//     after the user started talking again.
//
// Sensitive records (focus probe answered "not safe") are recorded but never
// persisted and never fed to the learning store; the caller passes the answer.
//
// Threading: Record() runs on the injection thread, Snapshot()/Consume() on the
// UI thread. All access is serialised by one mutex.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

namespace injection_ledger {

enum class InjectionMethod {
    Unknown = 0,
    ClipboardPaste,  // clipboard + Ctrl+V / WM_PASTE acknowledgement
    UnicodeInput,    // SendInput with KEYEVENTF_UNICODE
    WmChar,          // direct WM_CHAR posts (WeChat-style windows)
};

const char* MethodLogName(InjectionMethod method);

struct Record {
    std::wstring injectedText;
    // ASR text that produced this injection (before local post-processing is
    // already applied; kept for diagnostics and the organize hotkey).
    std::wstring sourceText;
    // Pre-selection snapshot for the undo path. DEAD UNTIL S3 LANDS: the only
    // writer is text_injector's capturePreSelection branch, which is gated on
    // Config::asrUndoEnabled (default false because S3 does not exist yet), so
    // hasPreSelection stays false and these fields carry nothing. S3 owns them;
    // if S3 is delayed indefinitely, remove the fields instead of letting them
    // rot (AGENTS.md class C).
    std::wstring preSelectionText;
    InjectionMethod method = InjectionMethod::Unknown;
    HWND targetWindow = nullptr;  // top level window the injection went to
    HWND focusWindow = nullptr;   // focused child at injection time (may be null)
    DWORD tickMs = 0;             // GetTickCount() at injection time
    uint64_t sequence = 0;
    bool hasPreSelection = false;
    bool sensitive = false;
    // Ditto: recorded for the S3/S5b consumers, unread until they exist.
    bool llmApplied = false;
    bool consumed = false;

    bool Valid() const { return sequence != 0 && !consumed && !injectedText.empty(); }
};

// Replaces the slot. Returns the new sequence number. (Named RecordInjection
// rather than Record: a class and a function sharing one name are ambiguous.)
uint64_t RecordInjection(Record record);

// Copies the current slot. Returns false when nothing usable is recorded
// (never recorded, consumed, or invalidated).
bool Snapshot(Record& out);

// Drops the current record (new recording started, or the app decided the
// context no longer matches).
void Invalidate();

// Marks the current record as acted upon; it will not be reported again.
void Consume();

// Monotonic counter shared by every producer (also used to build trace ids).
uint64_t NextSequence();

} // namespace injection_ledger
