#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

namespace asr_metrics {

void Reset();

void SetVadMs(double ms);
double GetVadMs();

void SetAsrDecodeMs(double ms);
double GetAsrDecodeMs();

void SetPunctMs(double ms);
double GetPunctMs();

void SetCloudApiMs(double ms);
double GetCloudApiMs();

void SetLlmMs(double ms);
double GetLlmMs();

void SetVadModelName(const std::wstring& name);
std::wstring GetVadModelName();

void SetVadTrimmedSamples(size_t count);
size_t GetVadTrimmedSamples();

} // namespace asr_metrics

extern std::mutex g_vadMetricsMutex;
extern std::wstring g_vadModelName;
extern std::atomic<size_t> g_vadTrimmedSamples;
extern std::atomic<double> g_vadMs;
extern std::atomic<double> g_asrDecodeMs;
extern std::atomic<double> g_punctMs;
extern std::atomic<double> g_cloudApiMs;
extern std::atomic<double> g_llmMs;

