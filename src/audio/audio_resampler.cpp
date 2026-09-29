#include "audio_resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// Modified Bessel function of the first kind I0(x)
double BesselI0(double x) {
    double sum = 1.0;
    double u = 1.0;
    const double halfx = x * 0.5;
    for (int i = 1; i <= 25; ++i) {
        u *= (halfx / i);
        sum += u * u;
        if (u * u < sum * 1e-16) break;
    }
    return sum;
}

} // namespace

AudioResampler::AudioResampler() = default;

bool AudioResampler::Init(uint32_t inRate, uint32_t outRate) {
    if (inRate == 0 || outRate == 0) return false;
    inRate_ = inRate;
    outRate_ = outRate;
    isPassthrough_ = (inRate == outRate);
    is48to16_ = (inRate == 48000 && outRate == 16000);

    if (isPassthrough_) {
        initialized_ = true;
        Reset();
        return true;
    }

    if (is48to16_) {
        InitKaiser48to16();
    } else {
        InitPolyphaseSinc(inRate, outRate);
    }

    initialized_ = true;
    Reset();
    return true;
}

void AudioResampler::InitKaiser48to16() {
    constexpr size_t N = kKaiserFilterLength; // 348
    filterCoeffs_.resize(N);

    // Filter design parameters:
    // fc = 7500 Hz, fs = 48000 Hz, stopband attenuation A = 60 dB
    // Kaiser beta = 0.1102 * (60 - 8.7) = 5.6533
    constexpr double kBeta = 5.6533;
    const double i0Beta = BesselI0(kBeta);
    constexpr double kFc = 7500.0;
    constexpr double kFs = 48000.0;
    constexpr double omegaC = 2.0 * std::numbers::pi * (kFc / kFs);
    constexpr double m0 = (static_cast<double>(N) - 1.0) / 2.0; // 173.5

    double sum = 0.0;
    for (size_t n = 0; n < N; ++n) {
        const double x = static_cast<double>(n) - m0;
        // Ideal lowpass impulse response: sin(omegaC * x) / (pi * x)
        const double ideal = (std::abs(x) < 1e-9)
            ? (omegaC / std::numbers::pi)
            : (std::sin(omegaC * x) / (std::numbers::pi * x));

        // Kaiser window
        const double t = (2.0 * static_cast<double>(n) - (static_cast<double>(N) - 1.0)) /
                         (static_cast<double>(N) - 1.0);
        const double arg = std::max(0.0, 1.0 - t * t);
        const double win = BesselI0(kBeta * std::sqrt(arg)) / i0Beta;

        const double val = ideal * win;
        filterCoeffs_[n] = static_cast<float>(val);
        sum += val;
    }

    // Normalize DC gain to 1.0 (0 dB)
    if (sum != 0.0) {
        const float norm = static_cast<float>(1.0 / sum);
        for (size_t n = 0; n < N; ++n) {
            filterCoeffs_[n] *= norm;
        }
    }
}

void AudioResampler::InitPolyphaseSinc(uint32_t inRate, uint32_t outRate) {
    resampleRatio_ = static_cast<double>(outRate) / static_cast<double>(inRate);
    resamplePhase_ = 0.0;

    // Sinc table with cutoff at min(inRate, outRate) * 0.45
    const double cutoff = std::min(1.0, resampleRatio_) * 0.45;
    constexpr size_t numPhases = kSincPhases;
    constexpr size_t halfTaps = kSincHalfTaps;
    constexpr size_t filterTaps = 2 * halfTaps;
    sincTable_.resize(numPhases * filterTaps);

    for (size_t p = 0; p < numPhases; ++p) {
        const double phaseFraction = static_cast<double>(p) / static_cast<double>(numPhases);
        double phaseSum = 0.0;
        for (size_t k = 0; k < filterTaps; ++k) {
            const double t = static_cast<double>(k) - static_cast<double>(halfTaps) - phaseFraction;
            // Hann-windowed sinc
            double sinc = 1.0;
            if (std::abs(t) > 1e-9) {
                const double piT = std::numbers::pi * t * cutoff;
                sinc = std::sin(piT) / piT;
            }
            double win = 0.0;
            if (std::abs(t) < static_cast<double>(halfTaps)) {
                win = 0.5 * (1.0 + std::cos(std::numbers::pi * t / static_cast<double>(halfTaps)));
            }
            const double val = sinc * win;
            sincTable_[p * filterTaps + k] = static_cast<float>(val);
            phaseSum += val;
        }
        if (phaseSum != 0.0) {
            const float norm = static_cast<float>(1.0 / phaseSum);
            for (size_t k = 0; k < filterTaps; ++k) {
                sincTable_[p * filterTaps + k] *= norm;
            }
        }
    }
}

void AudioResampler::Reset() {
    if (!initialized_) return;
    if (is48to16_) {
        // Pre-fill history with zeros equal to the filter group delay (N / 2 = 174 samples)
        // so that the first output sample corresponds to the start of the recording.
        historyBuffer_.assign(kKaiserFilterLength / 2, 0.0f);
        scratchCombined_.clear();
    } else {
        sincHistory_.assign(kSincHalfTaps, 0.0f);
        resamplePhase_ = 0.0;
    }
}

size_t AudioResampler::Process(std::span<const float> inSamples, std::vector<int16_t>& outPcm) {
    if (!initialized_ || inSamples.empty()) return 0;

    // 1. Direct pass-through (16k -> 16k)
    if (isPassthrough_) {
        const size_t count = inSamples.size();
        outPcm.reserve(outPcm.size() + count);
        for (float s : inSamples) {
            const float clamped = std::clamp(s, -1.0f, 1.0f);
            outPcm.push_back(static_cast<int16_t>(std::clamp(clamped * 32768.0f, -32768.0f, 32767.0f)));
        }
        return count;
    }

    // 2. High-performance 48k -> 16k Kaiser FIR decimation
    if (is48to16_) {
        scratchCombined_.clear();
        scratchCombined_.insert(scratchCombined_.end(), historyBuffer_.begin(), historyBuffer_.end());
        scratchCombined_.insert(scratchCombined_.end(), inSamples.begin(), inSamples.end());

        const size_t total = scratchCombined_.size();
        constexpr size_t N = kKaiserFilterLength; // 348
        constexpr size_t M = kKaiserDecimation;   // 3

        if (total < N) {
            historyBuffer_ = scratchCombined_;
            return 0;
        }

        const size_t maxOutputs = (total - N) / M + 1;
        outPcm.reserve(outPcm.size() + maxOutputs);

        const float* coeffs = filterCoeffs_.data();
        const float* inputPtr = scratchCombined_.data();
        size_t inIdx = 0;
        size_t samplesProduced = 0;

        while (inIdx + N <= total) {
            float acc = 0.0f;
            const float* window = inputPtr + inIdx;
            for (size_t k = 0; k < N; ++k) {
                acc += coeffs[k] * window[k];
            }
            const float clamped = std::clamp(acc, -1.0f, 1.0f);
            const int16_t s16 = static_cast<int16_t>(std::clamp(clamped * 32768.0f, -32768.0f, 32767.0f));
            outPcm.push_back(s16);
            ++samplesProduced;
            inIdx += M;
        }

        // Retain remaining samples from inIdx onwards as history for next chunk
        historyBuffer_.assign(scratchCombined_.begin() + inIdx, scratchCombined_.end());
        return samplesProduced;
    }

    // 3. Fractional ratio fallback (e.g. 44.1k -> 16k)
    scratchCombined_.clear();
    scratchCombined_.insert(scratchCombined_.end(), sincHistory_.begin(), sincHistory_.end());
    scratchCombined_.insert(scratchCombined_.end(), inSamples.begin(), inSamples.end());

    constexpr size_t halfTaps = kSincHalfTaps;
    constexpr size_t filterTaps = 2 * halfTaps;
    const size_t total = scratchCombined_.size();

    if (total < filterTaps) {
        sincHistory_ = scratchCombined_;
        return 0;
    }

    size_t samplesProduced = 0;
    const float* inputPtr = scratchCombined_.data();

    while (true) {
        const size_t idx = static_cast<size_t>(resamplePhase_);
        if (idx + filterTaps > total) break;

        const double frac = resamplePhase_ - static_cast<double>(idx);
        const size_t phaseIdx = static_cast<size_t>(frac * static_cast<double>(kSincPhases)) % kSincPhases;
        const float* phaseFilter = sincTable_.data() + phaseIdx * filterTaps;

        float acc = 0.0f;
        const float* window = inputPtr + idx;
        for (size_t k = 0; k < filterTaps; ++k) {
            acc += phaseFilter[k] * window[k];
        }

        const float clamped = std::clamp(acc, -1.0f, 1.0f);
        const int16_t s16 = static_cast<int16_t>(std::clamp(clamped * 32768.0f, -32768.0f, 32767.0f));
        outPcm.push_back(s16);
        ++samplesProduced;

        resamplePhase_ += (1.0 / resampleRatio_);
    }

    const size_t consumed = static_cast<size_t>(resamplePhase_);
    resamplePhase_ -= static_cast<double>(consumed);

    if (consumed < total) {
        sincHistory_.assign(scratchCombined_.begin() + consumed, scratchCombined_.end());
    } else {
        sincHistory_.clear();
    }

    return samplesProduced;
}
