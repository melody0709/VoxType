#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "vad_trim_core.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "vad_detector.h"

struct Config;

struct StreamingVadTrimStats {
    size_t rawBytes = 0;
    size_t outputBytes = 0;
    bool detectedSpeech = false;
    bool active = false;
    std::wstring modelName;
};

class StreamingVadTrimmer {
public:
    bool Start(const Config& config, IVadDetector& engine, std::wstring* error = nullptr);
    void Reset();

    bool IsActive() const { return active_.load(); }
    bool DetectedSpeech() const { return detectedSpeech_.load(); }
    size_t RawBytes() const { return core_.Stats().rawBytes; }
    size_t OutputBytes() const { return core_.Stats().outputBytes; }
    const std::wstring& ModelName() const { return modelName_; }
    StreamingVadTrimStats Stats() const;

    void ProcessPcm16(const BYTE* data, size_t bytes, std::vector<std::vector<BYTE>>& outputs);
    void Finish();

private:
    bool DetectVoice(const int16_t* samples, size_t sampleCount);

    IVadDetector* engine_ = nullptr;
    std::atomic<bool> active_{false};
    std::atomic<bool> detectedSpeech_{false};
    std::wstring vadModel_;
    std::wstring modelName_;
    VadTrimCore core_;
};

extern std::unique_ptr<StreamingVadTrimmer> g_streamingVadTrimmer;
extern std::vector<float> g_streamingVadSamples;
extern std::atomic<bool> g_streamingVadReady;

