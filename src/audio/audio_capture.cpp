#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "audio_capture.h"
#include "wasapi_capture.h"
#include "audio_diagnostics.h"
#include "audio_chunk_sink.h"
#include "vad_detector.h"
#include "streaming_vad_trimmer.h"
#include "debug_logger.h"
#include "utils.h"
#include "config_store.h"
#include "app_state.h"
#include "app_messages.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

HWAVEIN g_waveIn = nullptr;
WAVEHDR g_waveHeaders[8] = {};
std::vector<std::vector<BYTE>> g_waveBuffers;
std::vector<BYTE> g_audioData;
CRITICAL_SECTION g_audioLock;
std::atomic<bool> g_captureActive{false};
std::atomic<bool> g_captureSuppressed{false};
std::atomic<uint64_t> g_audioCaptureGeneration{0};
std::atomic<bool> g_audioCaptureFailurePending{false};
std::atomic<DWORD> g_audioCaptureFailureCode{0};
std::atomic<bool> g_audioCaptureFailureWasapi{false};
WasapiCapture g_wasapiCapture;

#pragma comment(lib, "winmm.lib")

static std::atomic<IVadDetector*> s_activeVadDetector{nullptr};

IVadDetector* GetActiveVadDetector() {
    return s_activeVadDetector.load(std::memory_order_acquire);
}

void SetActiveVadDetector(IVadDetector* detector) {
    s_activeVadDetector.store(detector, std::memory_order_release);
}

float CalculateAudioLevel(const BYTE* data, DWORD bytes) {
    if (!data || bytes < sizeof(int16_t)) return 0.0f;

    const auto* samples = reinterpret_cast<const int16_t*>(data);
    const size_t count = bytes / sizeof(int16_t);
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double v = static_cast<double>(samples[i]) / 32768.0;
        sum += v * v;
    }

    const double rms = std::sqrt(sum / static_cast<double>(count));
    const double db = 20.0 * std::log10(std::max(rms, 1e-6));
    const double normalized = (db + 50.0) / 40.0;
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

void CALLBACK WaveInProc(HWAVEIN waveIn, UINT msg, DWORD_PTR, DWORD_PTR param1, DWORD_PTR) {
    if (msg != WIM_DATA || waveIn != g_waveIn) return;
    auto* header = reinterpret_cast<WAVEHDR*>(param1);
    if (!header) return;

    if (header->dwBytesRecorded > 0 &&
        !g_captureSuppressed.load(std::memory_order_acquire)) {
        const BYTE* begin = reinterpret_cast<const BYTE*>(header->lpData);
        audio_diagnostics::RecordWaveInPcm16(begin, header->dwBytesRecorded);
        g_audioLevel.store(CalculateAudioLevel(begin, header->dwBytesRecorded));

        EnterCriticalSection(&g_audioLock);
        if (!g_captureSuppressed.load(std::memory_order_acquire)) {
            g_audioData.insert(g_audioData.end(), begin, begin + header->dwBytesRecorded);
            EnterCriticalSection(&g_streamingSessionCs);
            IAudioChunkSink* sink = GetActiveAudioChunkSink();
            if (sink && sink->IsRunning()) {
                const bool useStreamingVadTrim = g_streamingVadTrimmer && g_streamingVadTrimmer->IsActive();
                if (useStreamingVadTrim) {
                    std::vector<std::vector<BYTE>> streamingOutputs;
                    g_streamingVadTrimmer->ProcessPcm16(begin, header->dwBytesRecorded, streamingOutputs);
                    for (const auto& chunk : streamingOutputs) {
                        if (!chunk.empty()) {
                            sink->EnqueuePcmChunk(chunk.data(), chunk.size());
                        }
                    }
                } else {
                    sink->EnqueuePcmChunk(begin, header->dwBytesRecorded);
                }
            }
            LeaveCriticalSection(&g_streamingSessionCs);
        }
        LeaveCriticalSection(&g_audioLock);
    }

    if (g_captureActive) {
        header->dwBytesRecorded = 0;
        const MMRESULT result = waveInAddBuffer(waveIn, header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR && g_captureActive) {
            g_audioCaptureFailureCode.store(static_cast<DWORD>(result), std::memory_order_relaxed);
            g_audioCaptureFailureWasapi.store(false, std::memory_order_relaxed);
            g_audioCaptureFailurePending.store(true, std::memory_order_release);
            g_captureActive = false;
            if (g_mainWindow) {
                const uint64_t generation = g_audioCaptureGeneration.load(std::memory_order_acquire);
                PostMessageW(g_mainWindow,
                             kWaveInCaptureErrorMessage,
                             static_cast<WPARAM>(generation),
                             static_cast<LPARAM>(result));
            }
        }
    }
}

bool StartAudioCapture(std::wstring& error,
                       AudioCaptureStartFailure* failure) {
    if (failure) *failure = {};
    if (g_mainWindow) KillTimer(g_mainWindow, kMicKeepAliveTimer);

    const bool deviceOpen = g_waveIn || g_wasapiCapture.IsInitialized();
    if (deviceOpen && g_audioCaptureFailurePending.load(std::memory_order_acquire)) {
        CloseAudioCapture();
    } else if (deviceOpen) {
        if (g_captureSuppressed.load(std::memory_order_acquire)) {
            if (audio_diagnostics::NormalizeMode(g_config.diagnosticAudioMode) != L"off" ||
                g_config.enableDebugMode) {
                audio_diagnostics::ResetCapture();
            } else {
                audio_diagnostics::CancelCapture();
            }
            g_audioLevel.store(0.0f);
            EnterCriticalSection(&g_audioLock);
            g_audioData.clear();
            g_captureSuppressed.store(false, std::memory_order_release);
            LeaveCriticalSection(&g_audioLock);
            debug_log::Write("event=capture_keepalive_resume backend=%s",
                             g_wasapiCapture.IsInitialized() ? "wasapi" : "wavein");
        }
        return true;
    }
    g_captureSuppressed.store(false, std::memory_order_release);

    const uint64_t generation =
        g_audioCaptureGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_audioCaptureFailureCode.store(0, std::memory_order_relaxed);
    g_audioCaptureFailureWasapi.store(false, std::memory_order_relaxed);
    g_audioCaptureFailurePending.store(false, std::memory_order_release);

    if (audio_diagnostics::NormalizeMode(g_config.diagnosticAudioMode) != L"off" ||
        g_config.enableDebugMode) {
        audio_diagnostics::ResetCapture();
    } else {
        audio_diagnostics::CancelCapture();
    }

    g_audioLevel.store(0.0f);
    EnterCriticalSection(&g_audioLock);
    g_audioData.clear();
    LeaveCriticalSection(&g_audioLock);

    const bool attemptedWasapi = g_config.audioBackend == L"wasapi";
    std::wstring attemptedDeviceName;
    std::wstring attemptedDeviceId = g_config.audioDeviceId;
    bool attemptedDefaultDevice = g_config.audioDeviceId.empty();
    DWORD attemptedNativeSampleRate = 0;
    WORD attemptedNativeChannels = 0;
    WORD attemptedNativeBits = 0;
    bool attemptedNativeFloat = false;

    if (attemptedWasapi) {
        g_wasapiCapture.SetCaptureGeneration(generation);
        if (g_wasapiCapture.Init(g_config.audioDeviceId)) {
            attemptedDeviceName = g_wasapiCapture.GetDeviceName();
            attemptedDeviceId = g_wasapiCapture.GetDeviceId();
            attemptedDefaultDevice = g_wasapiCapture.UsedDefaultDevice();
            attemptedNativeSampleRate = g_wasapiCapture.GetNativeSampleRate();
            attemptedNativeChannels = static_cast<WORD>(g_wasapiCapture.GetNativeChannels());
            attemptedNativeBits = static_cast<WORD>(g_wasapiCapture.GetNativeBits());
            attemptedNativeFloat = g_wasapiCapture.GetNativeIsFloat();
            if (g_wasapiCapture.Start(error)) {
                audio_diagnostics::CaptureDeviceInfo device;
                device.backend = L"wasapi";
                device.deviceName = g_wasapiCapture.GetDeviceName();
                device.deviceId = g_wasapiCapture.GetDeviceId();
                device.usedDefaultDevice = g_wasapiCapture.UsedDefaultDevice();
                device.nativeSampleRate = g_wasapiCapture.GetNativeSampleRate();
                device.nativeChannels = static_cast<WORD>(g_wasapiCapture.GetNativeChannels());
                device.nativeBitsPerSample = static_cast<WORD>(g_wasapiCapture.GetNativeBits());
                device.nativeIsFloat = g_wasapiCapture.GetNativeIsFloat();
                audio_diagnostics::SetCaptureDeviceInfo(device);
                g_captureActive = true;
                return true;
            }
            debug_log::Write("[Audio] WASAPI Start failed: %s", WideToUtf8(error).c_str());
            g_wasapiCapture.Release();
            error.clear();
        } else {
            g_wasapiCapture.Release();
            debug_log::Write("[Audio] WASAPI Init failed, falling back to waveIn");
        }
    }

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 16000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    MMRESULT result = waveInOpen(&g_waveIn, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(WaveInProc), 0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        wchar_t detail[MAXERRORLENGTH] = {};
        waveInGetErrorTextW(result, detail, MAXERRORLENGTH);
        error = L"Microphone open failed";
        if (detail[0] != L'\0') error += L": " + std::wstring(detail);
        g_waveIn = nullptr;
        if (failure) {
            failure->attemptedBackends = attemptedWasapi ? L"wasapi,wavein" : L"wavein";
            failure->terminalBackend = L"wavein";
            failure->phase = L"open";
            failure->code = static_cast<DWORD>(result);
            failure->deviceName = attemptedDeviceName;
            failure->deviceId = attemptedDeviceId;
            failure->usedDefaultDevice = attemptedDefaultDevice;
            failure->nativeSampleRate = attemptedNativeSampleRate;
            failure->nativeChannels = attemptedNativeChannels;
            failure->nativeBitsPerSample = attemptedNativeBits;
            failure->nativeIsFloat = attemptedNativeFloat;
        }
        audio_diagnostics::CancelCapture();
        return false;
    }

    audio_diagnostics::CaptureDeviceInfo waveInDevice;
    waveInDevice.backend = L"wavein";
    waveInDevice.usedDefaultDevice = true;
    waveInDevice.nativeSampleRate = format.nSamplesPerSec;
    waveInDevice.nativeChannels = format.nChannels;
    waveInDevice.nativeBitsPerSample = format.wBitsPerSample;
    waveInDevice.nativeIsFloat = false;
    UINT deviceId = WAVE_MAPPER;
    if (waveInGetID(g_waveIn, &deviceId) == MMSYSERR_NOERROR) {
        WAVEINCAPSW caps = {};
        if (waveInGetDevCapsW(deviceId, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            waveInDevice.deviceName = caps.szPname;
        }
        waveInDevice.deviceId = L"wavein:" + std::to_wstring(deviceId);
    }
    audio_diagnostics::SetCaptureDeviceInfo(waveInDevice);

    g_waveBuffers.assign(8, std::vector<BYTE>(format.nAvgBytesPerSec / 50));
    ZeroMemory(g_waveHeaders, sizeof(g_waveHeaders));
    g_captureActive = true;

    size_t preparedHeaders = 0;
    std::wstring bufferFailurePhase;
    for (size_t i = 0; i < g_waveBuffers.size(); ++i) {
        g_waveHeaders[i].lpData = reinterpret_cast<LPSTR>(g_waveBuffers[i].data());
        g_waveHeaders[i].dwBufferLength = static_cast<DWORD>(g_waveBuffers[i].size());
        result = waveInPrepareHeader(g_waveIn, &g_waveHeaders[i], sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            error = L"Microphone buffer preparation failed";
            bufferFailurePhase = L"prepare_buffer";
            break;
        }
        ++preparedHeaders;
        result = waveInAddBuffer(g_waveIn, &g_waveHeaders[i], sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            error = L"Microphone buffer queue failed";
            bufferFailurePhase = L"queue_buffer";
            break;
        }
    }

    if (result != MMSYSERR_NOERROR) {
        wchar_t detail[MAXERRORLENGTH] = {};
        waveInGetErrorTextW(result, detail, MAXERRORLENGTH);
        if (detail[0] != L'\0') error += L": " + std::wstring(detail);
        if (failure) {
            failure->attemptedBackends = attemptedWasapi ? L"wasapi,wavein" : L"wavein";
            failure->terminalBackend = L"wavein";
            failure->phase = bufferFailurePhase.empty()
                ? L"prepare_or_queue_buffer"
                : bufferFailurePhase;
            failure->code = static_cast<DWORD>(result);
            failure->deviceName = waveInDevice.deviceName;
            failure->deviceId = waveInDevice.deviceId;
            failure->usedDefaultDevice = waveInDevice.usedDefaultDevice;
            failure->nativeSampleRate = waveInDevice.nativeSampleRate;
            failure->nativeChannels = waveInDevice.nativeChannels;
            failure->nativeBitsPerSample = waveInDevice.nativeBitsPerSample;
            failure->nativeIsFloat = waveInDevice.nativeIsFloat;
        }
        g_captureActive = false;
        waveInReset(g_waveIn);
        for (size_t i = 0; i < preparedHeaders; ++i) {
            waveInUnprepareHeader(g_waveIn, &g_waveHeaders[i], sizeof(WAVEHDR));
        }
        waveInClose(g_waveIn);
        g_waveIn = nullptr;
        audio_diagnostics::CancelCapture();
        return false;
    }

    result = waveInStart(g_waveIn);
    if (result != MMSYSERR_NOERROR) {
        wchar_t detail[MAXERRORLENGTH] = {};
        waveInGetErrorTextW(result, detail, MAXERRORLENGTH);
        error = L"Microphone start failed";
        if (detail[0] != L'\0') error += L": " + std::wstring(detail);
        if (failure) {
            failure->attemptedBackends = attemptedWasapi ? L"wasapi,wavein" : L"wavein";
            failure->terminalBackend = L"wavein";
            failure->phase = L"start";
            failure->code = static_cast<DWORD>(result);
            failure->deviceName = waveInDevice.deviceName;
            failure->deviceId = waveInDevice.deviceId;
            failure->usedDefaultDevice = waveInDevice.usedDefaultDevice;
            failure->nativeSampleRate = waveInDevice.nativeSampleRate;
            failure->nativeChannels = waveInDevice.nativeChannels;
            failure->nativeBitsPerSample = waveInDevice.nativeBitsPerSample;
            failure->nativeIsFloat = waveInDevice.nativeIsFloat;
        }
        g_captureActive = false;
        waveInReset(g_waveIn);
        for (size_t i = 0; i < preparedHeaders; ++i) {
            waveInUnprepareHeader(g_waveIn, &g_waveHeaders[i], sizeof(WAVEHDR));
        }
        waveInClose(g_waveIn);
        g_waveIn = nullptr;
        audio_diagnostics::CancelCapture();
        return false;
    }

    return true;
}

std::vector<BYTE> StopAudioCapture() {
    std::vector<BYTE> data;
    EnterCriticalSection(&g_audioLock);
    g_captureSuppressed.store(true, std::memory_order_release);
    data = g_audioData;
    g_audioData.clear();
    LeaveCriticalSection(&g_audioLock);
    g_audioLevel.store(0.0f);

    const bool deviceOpen = g_wasapiCapture.IsInitialized() || g_waveIn != nullptr;
    if (deviceOpen && g_mainWindow) {
        SetTimer(g_mainWindow, kMicKeepAliveTimer, kMicKeepAliveMs, nullptr);
    }
    return data;
}

void CloseAudioCapture() {
    if (g_mainWindow) KillTimer(g_mainWindow, kMicKeepAliveTimer);
    g_captureSuppressed.store(true, std::memory_order_release);
    g_audioCaptureGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_captureActive = false;
    if (g_wasapiCapture.IsInitialized()) {
        g_wasapiCapture.Stop();
        g_wasapiCapture.Release();
    } else if (g_waveIn) {
        waveInStop(g_waveIn);
        waveInReset(g_waveIn);
        for (auto& header : g_waveHeaders) {
            waveInUnprepareHeader(g_waveIn, &header, sizeof(WAVEHDR));
        }
        waveInClose(g_waveIn);
        g_waveIn = nullptr;
    }
    g_audioLevel.store(0.0f);

    EnterCriticalSection(&g_audioLock);
    g_audioData.clear();
    g_captureSuppressed.store(false, std::memory_order_release);
    LeaveCriticalSection(&g_audioLock);
    g_waveBuffers.clear();
}

std::vector<float> PcmToFloat(const std::vector<BYTE>& pcm) {
    const size_t count = pcm.size() / 2;
    std::vector<float> samples(count);
    const auto* raw = reinterpret_cast<const int16_t*>(pcm.data());
    for (size_t i = 0; i < count; ++i) {
        samples[i] = static_cast<float>(raw[i]) / 32768.0f;
    }
    return samples;
}
