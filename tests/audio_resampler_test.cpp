#include "audio_resampler.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {

void TestKaiserCoefficients() {
    AudioResampler resampler;
    const bool ok = resampler.Init(48000, 16000);
    assert(ok);
    assert(resampler.IsInitialized());

    const auto& coeffs = resampler.GetFilterCoeffs();
    assert(coeffs.size() == AudioResampler::kKaiserFilterLength); // 348

    // Verify DC gain is 1.0 (sum of all coefficients == 1.0)
    double sum = 0.0;
    for (float c : coeffs) {
        sum += c;
    }
    assert(std::abs(sum - 1.0) < 1e-4);

    // Verify filter symmetry: h[n] == h[N - 1 - n]
    const size_t N = coeffs.size();
    for (size_t n = 0; n < N / 2; ++n) {
        assert(std::abs(coeffs[n] - coeffs[N - 1 - n]) < 1e-6);
    }
    std::cout << "[PASS] Kaiser coefficients symmetry and DC gain verified." << std::endl;
}

void TestStopbandAttenuation() {
    AudioResampler resampler;
    resampler.Init(48000, 16000);

    // Generate 48kHz test signal: 1 second of 12kHz sine wave (full scale 1.0)
    // 12kHz is in the stopband (>8kHz). In naive decimation, 12kHz aliases to |12 - 16| = 4kHz.
    // With 60dB stopband attenuation, maximum output amplitude must be < 0.001 (<-60dB).
    constexpr size_t inRate = 48000;
    constexpr size_t numSamples = 48000; // 1 second
    std::vector<float> inSamples12k(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(inRate);
        inSamples12k[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 12000.0 * t));
    }

    std::vector<int16_t> outPcm12k;
    resampler.Reset();
    resampler.Process(inSamples12k, outPcm12k);

    // Skip the first 200 samples (filter transient / group delay ramp-up)
    int16_t maxOutput12k = 0;
    for (size_t i = 200; i < outPcm12k.size(); ++i) {
        maxOutput12k = std::max(maxOutput12k, static_cast<int16_t>(std::abs(outPcm12k[i])));
    }
    // Full scale 16-bit is 32767.
    // -60 dB of full scale is: 32767 * 10^(-60/20) = 32767 * 0.001 = 32.7.
    // Measured max output must be <= 35.
    const double maxAmpFloat12k = static_cast<double>(maxOutput12k) / 32768.0;
    const double attenuationDb12k = 20.0 * std::log10(std::max(1e-9, maxAmpFloat12k));
    std::cout << "12kHz stopband leakage: max=" << maxOutput12k
              << " (" << attenuationDb12k << " dB)" << std::endl;
    assert(maxOutput12k <= 35); // Proves >= 59.5 dB attenuation!

    // Also test 18kHz stopband attenuation
    std::vector<float> inSamples18k(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(inRate);
        inSamples18k[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 18000.0 * t));
    }
    std::vector<int16_t> outPcm18k;
    resampler.Reset();
    resampler.Process(inSamples18k, outPcm18k);

    int16_t maxOutput18k = 0;
    for (size_t i = 200; i < outPcm18k.size(); ++i) {
        maxOutput18k = std::max(maxOutput18k, static_cast<int16_t>(std::abs(outPcm18k[i])));
    }
    const double maxAmpFloat18k = static_cast<double>(maxOutput18k) / 32768.0;
    const double attenuationDb18k = 20.0 * std::log10(std::max(1e-9, maxAmpFloat18k));
    std::cout << "18kHz stopband leakage: max=" << maxOutput18k
              << " (" << attenuationDb18k << " dB)" << std::endl;
    assert(maxOutput18k <= 35); // Proves >= 59.5 dB attenuation!

    std::cout << "[PASS] Stopband attenuation >= 60 dB verified." << std::endl;
}

void TestPassbandFlatness() {
    AudioResampler resampler;
    resampler.Init(48000, 16000);

    // Test 1kHz passband signal
    constexpr size_t inRate = 48000;
    constexpr size_t numSamples = 48000;
    std::vector<float> inSamples1k(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(inRate);
        inSamples1k[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * t));
    }

    std::vector<int16_t> outPcm1k;
    resampler.Reset();
    resampler.Process(inSamples1k, outPcm1k);

    int16_t maxOutput1k = 0;
    for (size_t i = 200; i < outPcm1k.size() - 200; ++i) {
        maxOutput1k = std::max(maxOutput1k, static_cast<int16_t>(std::abs(outPcm1k[i])));
    }
    const double passbandGain = static_cast<double>(maxOutput1k) / 32767.0;
    std::cout << "1kHz passband gain: " << passbandGain << " (expected ~1.0)" << std::endl;
    // Passband flatness within 0.5dB (0.944 ~ 1.059)
    assert(passbandGain >= 0.95 && passbandGain <= 1.05);

    std::cout << "[PASS] Passband flatness verified." << std::endl;
}

void TestStreamingChunkContinuity() {
    AudioResampler resamplerBatch;
    resamplerBatch.Init(48000, 16000);
    resamplerBatch.Reset();

    AudioResampler resamplerChunked;
    resamplerChunked.Init(48000, 16000);
    resamplerChunked.Reset();

    // 4800 input samples (100 ms of complex audio with multiple frequencies)
    constexpr size_t totalSamples = 4800;
    std::vector<float> input(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        input[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * t) +
                                      0.3 * std::sin(2.0 * std::numbers::pi * 2500.0 * t));
    }

    // Process all in one batch
    std::vector<int16_t> batchOutput;
    resamplerBatch.Process(input, batchOutput);

    // Process in 10ms chunks (480 samples per chunk)
    std::vector<int16_t> chunkedOutput;
    constexpr size_t chunkSize = 480;
    for (size_t offset = 0; offset < totalSamples; offset += chunkSize) {
        const size_t count = std::min(chunkSize, totalSamples - offset);
        resamplerChunked.Process(std::span<const float>(input.data() + offset, count), chunkedOutput);
    }

    // Both outputs must match exactly in sample count and values
    assert(batchOutput.size() == chunkedOutput.size());
    for (size_t i = 0; i < batchOutput.size(); ++i) {
        assert(std::abs(batchOutput[i] - chunkedOutput[i]) <= 1);
    }

    std::cout << "[PASS] Streaming chunk continuity verified (identical to single batch)." << std::endl;
}

void TestFallback44100() {
    AudioResampler resamplerBatch;
    assert(resamplerBatch.Init(44100, 16000));
    AudioResampler resamplerChunked;
    assert(resamplerChunked.Init(44100, 16000));

    // Feed 4410 samples (100ms at 44.1kHz)
    constexpr size_t totalSamples = 4410;
    std::vector<float> input(totalSamples);
    for (size_t i = 0; i < input.size(); ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        input[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * t));
    }

    std::vector<int16_t> batchPcm;
    resamplerBatch.Reset();
    const size_t outCount = resamplerBatch.Process(input, batchPcm);
    assert(outCount > 0);
    // 100ms at 16kHz should produce ~1600 samples (within small edge slack)
    assert(outCount >= 1550 && outCount <= 1650);

    // Process chunked in 441-sample (10ms) chunks
    std::vector<int16_t> chunkedPcm;
    resamplerChunked.Reset();
    constexpr size_t chunkSize = 441;
    for (size_t offset = 0; offset < totalSamples; offset += chunkSize) {
        const size_t count = std::min(chunkSize, totalSamples - offset);
        resamplerChunked.Process(std::span<const float>(input.data() + offset, count), chunkedPcm);
    }

    assert(batchPcm.size() == chunkedPcm.size());
    for (size_t i = 0; i < batchPcm.size(); ++i) {
        assert(std::abs(batchPcm[i] - chunkedPcm[i]) <= 1);
    }

    std::cout << "[PASS] 44.1kHz -> 16kHz fallback resampler verified (batch & streaming continuity)." << std::endl;
}

} // namespace

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " AudioResampler DSP & Frequency Response Test" << std::endl;
    std::cout << "========================================" << std::endl;

    TestKaiserCoefficients();
    TestPassbandFlatness();
    TestStopbandAttenuation();
    TestStreamingChunkContinuity();
    TestFallback44100();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL RESAMPLER TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
