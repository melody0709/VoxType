#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "asr_streaming_session.h"
#include "streaming_vad_trimmer.h"
#include <memory>
#include <string>
#include <vector>

struct HudUpdateWithOptionsMessage {
    uint64_t attemptId = 0;
    std::wstring statusLine;
    std::wstring text;
    float maxWidthDip = 0.0f;
    float maxScreenWidthFraction = 0.0f;
    int maxLines = 0;
    int fixedLines = 0;
    bool streamingPartial = false;
};

void StartRecordingSession();
void StopRecordingSession();

void BeginCaptureOnly();
void DiscardPendingCapture();

void HandleAudioCaptureFailure(uint64_t generation, DWORD code, bool wasapi);

std::unique_ptr<IStreamingAsrSession> TakeActiveStreamingSession();
bool HasActiveStreamingSession();
void AbortAndResetActiveStreamingSession();
void ActivateStreamingSession(std::unique_ptr<IStreamingAsrSession> session, bool useVadTrimmer);
void StartStreamingWatchdog(const std::wstring& listeningText);
inline void StartStreamingWatchdog(const wchar_t* listeningText) {
    StartStreamingWatchdog(listeningText ? std::wstring(listeningText) : std::wstring());
}

void ResetStreamingVadTrimmerState();
bool StartStreamingVadTrimmerForCloud(const Config& config, const wchar_t* debugPrefix, bool markReady = true);
void FinishStreamingVadTrimmer();
bool StreamingVadTrimSawNoSpeech();
bool GetStreamingVadTrimStats(StreamingVadTrimStats& stats);

void StreamingPartialHudCallback(const std::wstring& text, bool, void* userData);

bool GetWasapiUsed();
const std::wstring& GetWasapiDeviceName();
UINT32 GetWasapiNativeRate();
double GetRecordingMs();
ULONGLONG GetSessionStartTick();

bool IsCapturePendingOnly();
bool IsStopDelayPending();
void SetStopDelayPending(bool pending);
bool IsStopDelayRestoreCapsLock();
void SetStopDelayRestoreCapsLock(bool restore);
bool IsStopDelayHeldForRepress();
void SetStopDelayHeldForRepress(bool held);
void SetCaptureConfigStale(bool stale);

extern std::unique_ptr<IStreamingAsrSession> g_activeStreamingSession;

