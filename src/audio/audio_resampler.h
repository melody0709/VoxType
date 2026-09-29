#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// High-performance, anti-aliased audio resampler for speech recognition.
// For 48kHz -> 16kHz (3:1 decimation): uses a 348-tap Kaiser-windowed low-pass FIR
// filter with cutoff at 7.5 kHz, achieving >= 60 dB stopband attenuation.
// For arbitrary ratios (e.g. 44.1kHz -> 16kHz): uses bandlimited polyphase sinc.
class AudioResampler {
public:
    static constexpr uint32_t kKaiserFilterLength = 348;
    static constexpr uint32_t kKaiserDecimation = 3; // 48k -> 16k
    static constexpr uint32_t kKaiserTapsPerPhase = kKaiserFilterLength / kKaiserDecimation; // 116

    AudioResampler();
    ~AudioResampler() = default;

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    // Initializes resampler for inRate -> outRate.
    // Precomputes filter coefficients and prepares state buffers.
    bool Init(uint32_t inRate, uint32_t outRate);

    // Resets history buffer to zeros (call before a new recording starts).
    void Reset();

    // Process mono float samples (normalized -1.0 to 1.0).
    // Appends resampled 16-bit PCM samples to outPcm.
    // Returns number of int16_t samples appended.
    size_t Process(std::span<const float> inSamples, std::vector<int16_t>& outPcm);

    uint32_t InRate() const { return inRate_; }
    uint32_t OutRate() const { return outRate_; }
    bool IsInitialized() const { return initialized_; }

    // Direct access to prototype filter coefficients (for verification/testing)
    const std::vector<float>& GetFilterCoeffs() const { return filterCoeffs_; }

private:
    void InitKaiser48to16();
    void InitPolyphaseSinc(uint32_t inRate, uint32_t outRate);

    uint32_t inRate_{0};
    uint32_t outRate_{0};
    bool initialized_{false};
    bool is48to16_{false};
    bool isPassthrough_{false};

    // 48k -> 16k Kaiser FIR coefficients: 348 taps
    std::vector<float> filterCoeffs_;
    std::vector<float> historyBuffer_;

    // Arbitrary ratio fallback (e.g. 44100 -> 16000)
    static constexpr size_t kSincPhases = 64;
    static constexpr size_t kSincHalfTaps = 16;
    std::vector<float> sincTable_;
    std::vector<float> sincHistory_;
    double resamplePhase_{0.0};
    double resampleRatio_{1.0};

    // Scratch combined buffer reused across Process() calls to avoid heap allocations
    std::vector<float> scratchCombined_;
};
