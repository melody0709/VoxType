#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include "vad_detector.h"

#include "utils.h"
#include <string>
#include <vector>

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
