#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include "vad_detector.h"

#include "utils.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class WasapiCapture;

struct AudioCaptureStartFailure {
    std::wstring attemptedBackends;
    std::wstring terminalBackend;
    std::wstring phase;
    DWORD code = ERROR_SUCCESS;
    std::wstring deviceName;
    std::wstring deviceId;
    bool usedDefaultDevice = true;
    DWORD nativeSampleRate = 0;
    WORD nativeChannels = 0;
    WORD nativeBitsPerSample = 0;
    bool nativeIsFloat = false;
};

float CalculateAudioLevel(const BYTE* data, DWORD bytes);
void CALLBACK WaveInProc(HWAVEIN waveIn, UINT msg, DWORD_PTR, DWORD_PTR param1, DWORD_PTR);
bool StartAudioCapture(std::wstring& error, AudioCaptureStartFailure* failure = nullptr);
std::vector<BYTE> StopAudioCapture();
void CloseAudioCapture();
std::vector<float> PcmToFloat(const std::vector<BYTE>& pcm);

extern std::vector<BYTE> g_audioData;
extern CRITICAL_SECTION g_audioLock;
extern std::atomic<bool> g_captureActive;
extern std::atomic<bool> g_captureSuppressed;
extern std::atomic<uint64_t> g_audioCaptureGeneration;
extern std::atomic<bool> g_audioCaptureFailurePending;
extern std::atomic<DWORD> g_audioCaptureFailureCode;
extern std::atomic<bool> g_audioCaptureFailureWasapi;
extern HWAVEIN g_waveIn;
extern WAVEHDR g_waveHeaders[8];
extern std::vector<std::vector<BYTE>> g_waveBuffers;
extern WasapiCapture g_wasapiCapture;

