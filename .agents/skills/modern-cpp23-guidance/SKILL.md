---
name: modern-cpp23-guidance
description: >-
  Use this skill when developing, refactoring, or reviewing C++ code in VoxType.
  Enforces C++23 standards, zero-overhead modern idioms (std::expected, std::span,
  std::format, std::print, std::jthread, ranges), along with Windows Win32/WASAPI,
  Direct2D, and real-time audio constraints.
---

# Modern C++23 Development Guide for VoxType

This skill provides binding design patterns, modern idioms, and safety constraints for all C++ development in VoxType. VoxType is a high-performance Windows 11 desktop application combining Win32, Direct2D, WASAPI real-time audio, and local/cloud ASR engines.

All new code and refactorings must follow **C++23** standards.

---

## 1. Toolchain & Compilation Baseline

- **Standard**: C++23 (`set(CMAKE_CXX_STANDARD 23)` in `CMakeLists.txt`).
- **Compiler**: MSVC 19.4x+ (Visual Studio 2022 17.6+) mapping to `-std:c++latest`.
- **Encoding**: Compile with `/utf-8` enabled.
- **Authoritative Build**: Always build and verify via `build.bat` and `build.bat --test`. Never manually copy binaries or rely on ad-hoc scripts.

---

## 2. Core C++23 Idioms to Enforce

### A. Error Handling: `std::expected<T, E>`
- **Replace output parameters**: Eliminate legacy patterns like `bool DoWork(..., Result& out, std::string& err)`.
- **Use `std::expected`**: Return `std::expected<T, E>` for operations that can fail (e.g. JSON parsing, HTTP requests, token validation, audio format negotiation).
- **Leverage monadic operations**: Use `.and_then()`, `.transform()`, `.or_else()`, and `.value_or()` for expressive, flat error propagation chains.

### B. Audio & Data Slices: `std::span`
- **Zero-copy views**: Audio PCM streams (16kHz mono `float` or `int16_t`), acoustic feature matrices, and Opus packets must be passed as `std::span<const float>` or `std::span<const int16_t>`.
- **Ban raw pointer pairs**: Never pass `(const float* data, size_t count)` across internal function boundaries.
- **Prevent accidental copies**: Do not create temporary `std::vector` objects just to pass a slice of an audio buffer.

### C. Formatting & Printing: `std::format` & `std::print`
- **Ban `snprintf` / `sprintf`**: Formatting strings, JSON payloads, URLs, and diagnostic logs must use `std::format` (or `std::print` / `std::println` for console tools).
- **Type safety**: `std::format` is checked at compile time and eliminates buffer overflow hazards entirely.

### D. Concurrency & Threads: `std::jthread` & `std::stop_token`
- **Collaborative cancellation**: Background tasks (WASAPI audio capture, WebSocket receive loops, model preloading) should use `std::jthread` instead of `std::thread`.
- **Automatic join**: `std::jthread` automatically requests cancellation (`request_stop()`) and joins on destruction, eliminating thread leak and hang bugs.
- **Check stop tokens**: Long-running loops should inspect `std::stop_token::stop_requested()`.

### E. Range Algorithms & Views: `<ranges>`
- **Composability**: Use `std::views::filter`, `std::views::transform`, `std::ranges::find_if`, etc., to iterate and transform collections cleanly.
- **Avoid manual index-based loops** unless raw index arithmetic is strictly required for DSP filters.

### F. String Literals & Encoding (C++20/C++23 Rule)
- **Beware of `char8_t`**: In C++20 and C++23, `u8"..."` produces `const char8_t[]`, which does **not** implicitly convert to `std::string`.
- **Convention**: Since `/utf-8` is passed to MSVC, ordinary string literals `"..."` are already UTF-8 encoded in execution character set. Write `"你好世界"` directly rather than `u8"你好世界"` when initializing `std::string` or passing to Win32 UTF-8 APIs.

---

## 3. VoxType Architecture & Windows Constraints

### A. Real-Time Audio Callback (WASAPI)
- **Zero Allocation**: Real-time audio capture callbacks (`wasapi_capture.cpp`) must execute with minimal latency. **No `new`, `malloc`, or dynamic vector reallocation** inside the audio thread.
- **WASAPI Lifecycle**: The lifecycle sequence is strictly `Init -> Start -> Stop -> Release`. Never skip `Release()`—failing to call `Release()` leaves the audio endpoint locked and causes subsequent sessions to freeze.

### B. High-DPI & Coordinate Units
- **Direct2D / DirectWrite**: Render in DIPs (Device-Independent Pixels, 1/96 inch).
- **Win32 Window APIs**: Functions like `SetWindowPos`, `GetWindowRect`, and window message coordinates operate in **physical pixels**.
- **Never mix units**: Always convert DIPs to physical pixels via `dip * dpi / 96.0f` before passing to Win32 window APIs.

### C. DLL Delay Loading & SEH Safety
- Third-party libraries (`sherpa-onnx-cxx-api.dll`, `onnxruntime.dll`, `kaldi-native-fbank-core.dll`) are loaded lazily via `/DELAYLOAD`.
- Loading checks must use structured exception handling (`TryLoadAsrDlls()`) to prevent immediate process crash if a DLL is absent.

### D. VAD & Audio Trimming Semantics
- Public VAD trimming (`VadTrimCore`) only strips leading and trailing silence; it must never strip intra-speech pauses.
- `VadTrimCore::ProcessChunk()` has append semantics: callers must clear or manage local chunk outputs appropriately.
- Streaming VAD speech detection checks must rely on `StreamingVadTrimmer::DetectedSpeech()`.

---

## 4. Anti-Patterns (What NOT to do)

- ❌ Do NOT revert to C++17 paradigms (e.g. `snprintf`, pointer+length pairs, raw `std::thread` without cleanup).
- ❌ Do NOT perform blocking network calls or model inferences on the UI thread.
- ❌ Do NOT use `volatile bool` for thread synchronization; always use `std::atomic<bool>` or `std::stop_token`.
- ❌ Do NOT modify `CMakeLists.txt` or bypass CMake when adding dependencies; always pair `#pragma comment(lib)` with CMake definitions.
- ❌ Do NOT use `WM_COMMAND` / `WM_PASTE` for WeChat (Weixin.exe); WeChat uses custom Qt controls and requires character-by-character input via `WM_CHAR`.
