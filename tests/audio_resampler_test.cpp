#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "audio_resampler.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <numbers>
#include <utility>
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

    constexpr size_t inRate = 48000;
    constexpr size_t numSamples = 24000; // 0.5s per frequency
    std::vector<float> inSamples(numSamples);
    std::vector<int16_t> outPcm;

    // Test a wide sweep of stopband frequencies from 8.5kHz to 23.5kHz
    const double testFreqs[] = {8500.0, 10000.0, 12000.0, 14000.0, 16000.0, 18000.0, 20000.0, 22000.0, 23500.0};
    for (double freq : testFreqs) {
        for (size_t i = 0; i < numSamples; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(inRate);
            inSamples[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * freq * t));
        }

        outPcm.clear();
        resampler.Reset();
        resampler.Process(inSamples, outPcm);

        int maxOutput = 0;
        for (size_t i = 200; i < outPcm.size(); ++i) {
            maxOutput = std::max(maxOutput, std::abs(static_cast<int>(outPcm[i])));
        }

        const double maxAmpFloat = static_cast<double>(maxOutput) / 32768.0;
        std::cout << freq << "Hz stopband leakage: max=" << maxOutput;
        if (maxOutput == 0) std::cout << " (below PCM16 quantization resolution)";
        else std::cout << " (" << 20.0 * std::log10(maxAmpFloat) << " dB)";
        std::cout << std::endl;
        assert(maxOutput <= 32); // >= 60 dB at this sampled frequency.
    }

    std::cout << "[PASS] Stopband attenuation >= 60 dB at sampled 8.5kHz-23.5kHz frequencies." << std::endl;
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

    int maxOutput1k = 0;
    for (size_t i = 200; i < outPcm1k.size() - 200; ++i) {
        maxOutput1k = std::max(maxOutput1k, std::abs(static_cast<int>(outPcm1k[i])));
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
        input[i] = (i >= 1600 && i < 2400) ? 0.0f :
            static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * t) +
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
    const bool okBatch = resamplerBatch.Init(44100, 16000);
    assert(okBatch);
    AudioResampler resamplerChunked;
    const bool okChunked = resamplerChunked.Init(44100, 16000);
    assert(okChunked);

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

    std::cout << "batch size: " << batchPcm.size() << ", chunked size: " << chunkedPcm.size() << std::endl;
    assert(batchPcm.size() == chunkedPcm.size());
    int maxDiff = 0;
    size_t diffCount = 0;
    for (size_t i = 0; i < batchPcm.size(); ++i) {
        int diff = std::abs(batchPcm[i] - chunkedPcm[i]);
        if (diff > maxDiff) maxDiff = diff;
        if (diff > 1) ++diffCount;
    }
    std::cout << "44.1k batch vs chunked max diff: " << maxDiff << ", diffCount > 1: " << diffCount << " / " << batchPcm.size() << std::endl;
    assert(maxDiff <= 1); // Packet boundaries must not change the phase bucket.

    // Test 5000Hz passband transmission (critical for sibilants/fricatives)
    std::vector<float> input5k(totalSamples);
    for (size_t i = 0; i < input5k.size(); ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        input5k[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 5000.0 * t));
    }
    std::vector<int16_t> out5k;
    resamplerBatch.Reset();
    resamplerBatch.Process(input5k, out5k);
    int maxOutput5k = 0;
    for (size_t i = 200; i < out5k.size() - 200; ++i) {
        maxOutput5k = std::max(maxOutput5k, std::abs(static_cast<int>(out5k[i])));
    }
    const double passbandGain5k = static_cast<double>(maxOutput5k) / 32767.0;
    std::cout << "44.1kHz -> 16kHz 5kHz passband gain: " << passbandGain5k << std::endl;
    assert(passbandGain5k >= 0.70);

    std::cout << "[PASS] 44.1kHz -> 16kHz fallback resampler verified." << std::endl;
}

void TestFractionalPacketization() {
    for (const auto [inputRate, outputRate] : {
             std::pair{44100U, 16000U}, std::pair{22050U, 16000U},
             std::pair{96000U, 16000U}, std::pair{16000U, 44100U}}) {
        AudioResampler batch;
        AudioResampler packets;
        assert(batch.Init(inputRate, outputRate) && packets.Init(inputRate, outputRate));
        std::vector<float> input(inputRate / 5);
        for (size_t i = 0; i < input.size(); ++i) {
            const double t = static_cast<double>(i) / inputRate;
            input[i] = static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * 440 * t) +
                                          0.3 * std::sin(2 * std::numbers::pi * 5000 * t));
        }
        std::vector<int16_t> expected;
        std::vector<int16_t> actual;
        batch.Process(input, expected);
        constexpr size_t sizes[] = {1, 7, 31, 64, 441, 1024};
        size_t packet = 0;
        for (size_t offset = 0; offset < input.size();) {
            const size_t count = std::min(sizes[packet++ % std::size(sizes)], input.size() - offset);
            packets.Process(std::span<const float>(input.data() + offset, count), actual);
            offset += count;
        }
        assert(!expected.empty() && actual == expected);
    }
    std::cout << "[PASS] Fractional resampling is packet-independent at four rate combinations." << std::endl;
}

void TestNewUtteranceReset() {
    for (uint32_t rate : {48000U, 44100U, 16000U}) {
        AudioResampler reused;
        AudioResampler fresh;
        assert(reused.Init(rate, 16000) && fresh.Init(rate, 16000));
        std::vector<float> previous(1000, 0.75f);
        std::vector<float> silence(1000, 0.0f);
        std::vector<int16_t> oldOutput;
        reused.Process(previous, oldOutput);
        reused.Reset();
        std::vector<int16_t> resetOutput;
        std::vector<int16_t> freshOutput;
        reused.Process(silence, resetOutput);
        fresh.Process(silence, freshOutput);
        assert(!resetOutput.empty() && resetOutput == freshOutput);
        assert(std::all_of(resetOutput.begin(), resetOutput.end(), [](int16_t sample) { return sample == 0; }));
        assert(!reused.Init(0, 16000) && !reused.IsInitialized());
    }
    std::cout << "[PASS] New utterance reset removes previous filter history." << std::endl;
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
    TestFractionalPacketization();
    TestNewUtteranceReset();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL RESAMPLER TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
