#include "audio_chunk_sink.h"

#include <atomic>

namespace {
std::atomic<IAudioChunkSink*> g_activeSink{nullptr};
}

IAudioChunkSink* GetActiveAudioChunkSink() {
    return g_activeSink.load(std::memory_order_acquire);
}

void SetActiveAudioChunkSink(IAudioChunkSink* sink) {
    g_activeSink.store(sink, std::memory_order_release);
}
