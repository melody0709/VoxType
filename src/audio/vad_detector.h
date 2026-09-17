#pragma once

#include <cstddef>
#include <string>

struct Config;

class IVadDetector {
public:
    virtual ~IVadDetector() = default;
    virtual void Lock() = 0;
    virtual void Unlock() = 0;
    virtual bool EnsureVadForConfig(const Config& config, int threads) = 0;
    virtual void ResetVad(const std::wstring& vadModel) = 0;
    virtual bool DetectSpeech(const float* samples, size_t count, const std::wstring& vadModel) = 0;
};

IVadDetector* GetActiveVadDetector();
void SetActiveVadDetector(IVadDetector* detector);
