#include "asr_metrics.h"

std::mutex g_vadMetricsMutex;
std::wstring g_vadModelName;
std::atomic<size_t> g_vadTrimmedSamples{0};
std::atomic<double> g_vadMs{0.0};
std::atomic<double> g_asrDecodeMs{0.0};
std::atomic<double> g_punctMs{0.0};
std::atomic<double> g_cloudApiMs{0.0};
std::atomic<double> g_llmMs{0.0};

namespace asr_metrics {

void Reset() {
    g_vadMs.store(0.0, std::memory_order_relaxed);
    g_asrDecodeMs.store(0.0, std::memory_order_relaxed);
    g_punctMs.store(0.0, std::memory_order_relaxed);
    g_cloudApiMs.store(0.0, std::memory_order_relaxed);
    g_llmMs.store(0.0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
        g_vadModelName.clear();
    }
    g_vadTrimmedSamples.store(0, std::memory_order_relaxed);
}

void SetVadMs(double ms) { g_vadMs.store(ms, std::memory_order_relaxed); }
double GetVadMs() { return g_vadMs.load(std::memory_order_relaxed); }

void SetAsrDecodeMs(double ms) { g_asrDecodeMs.store(ms, std::memory_order_relaxed); }
double GetAsrDecodeMs() { return g_asrDecodeMs.load(std::memory_order_relaxed); }

void SetPunctMs(double ms) { g_punctMs.store(ms, std::memory_order_relaxed); }
double GetPunctMs() { return g_punctMs.load(std::memory_order_relaxed); }

void SetCloudApiMs(double ms) { g_cloudApiMs.store(ms, std::memory_order_relaxed); }
double GetCloudApiMs() { return g_cloudApiMs.load(std::memory_order_relaxed); }

void SetLlmMs(double ms) { g_llmMs.store(ms, std::memory_order_relaxed); }
double GetLlmMs() { return g_llmMs.load(std::memory_order_relaxed); }

void SetVadModelName(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
    g_vadModelName = name;
}
std::wstring GetVadModelName() {
    std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
    return g_vadModelName;
}

void SetVadTrimmedSamples(size_t count) { g_vadTrimmedSamples.store(count, std::memory_order_relaxed); }
size_t GetVadTrimmedSamples() { return g_vadTrimmedSamples.load(std::memory_order_relaxed); }

} // namespace asr_metrics

