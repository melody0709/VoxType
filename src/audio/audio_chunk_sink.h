#pragma once

#include <cstddef>

class IAudioChunkSink {
public:
    virtual ~IAudioChunkSink() = default;
    virtual bool IsRunning() const = 0;
    virtual bool EnqueuePcmChunk(const void* data, size_t bytes) = 0;
};

IAudioChunkSink* GetActiveAudioChunkSink();
void SetActiveAudioChunkSink(IAudioChunkSink* sink);
