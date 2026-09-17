#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "vad_detector.h"

struct Config;

struct BatchVadTrimResult {
    bool active = false;
    bool detectedSpeech = false;
    std::wstring modelName;
    std::wstring error;
    size_t rawBytes = 0;
    size_t outputBytes = 0;
    double elapsedMs = 0.0;
    std::vector<BYTE> pcm;
};

BatchVadTrimResult TrimBatchPcm16WithVad(const Config& config,
                                         IVadDetector& engine,
                                         const std::vector<BYTE>& pcm);
