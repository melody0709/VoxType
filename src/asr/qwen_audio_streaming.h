#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace qwen_audio_streaming {

struct Config {
    uint64_t attemptId = 0;
    std::wstring apiKey;
    std::wstring baseUrl;
    std::wstring model = L"qwen-audio-3.1-asr-flash-streaming";
    std::wstring languageHints;
    std::wstring vocabularyId;
    std::wstring vocabulary;
    // Optional focused input-field context sent in run-task.payload.input.
    std::wstring inputContextText;
    // Optional one-shot context refresh sent while the task is still running.
    bool enableContinueContext = false;
    // Audio 3 special-word filter, represented as newline-delimited lists.
    std::wstring specialWordReplaceList;
    std::wstring specialWordEmptyList;
    bool systemReservedFilter = false;
    bool semanticPunctuation = false;
    int maxSentenceSilenceMs = 1300;
    bool multiThresholdMode = false;
    bool heartbeat = false;
    bool speechNoiseThresholdEnabled = false;
    float speechNoiseThreshold = 0.0f;
    // Audio 3.1 streaming only (near_meeting_16k / far_field_meeting_16k).
    // The sender omits it for 3.0, which does not accept the field.
    std::wstring vadModel = L"far_field_meeting_16k";
    // Audio 3.1 streaming only; omitted for 3.0.
    bool keepDialect = false;
    // qwen-audio-3.1-asr-flash-message only; omitted for every other model.
    // Stays off by default so the model never rewrites user wording unless the
    // user opts in.
    bool disfluencyRemovalEnabled = false;
};

struct Event {
    std::wstring action;
    std::wstring text;
    std::wstring message;
    std::wstring errorCode;
    bool taskStarted = false;
    bool taskFinished = false;
    bool failed = false;
    bool timeout = false;
    bool sentenceEnd = false;
    bool heartbeat = false;
    // A WebSocket close frame before task-finished is a transport terminal
    // condition, not a provider task-failed event.
    bool peerClosed = false;
    // Provider may encode silence as task-failed/ASR_RESPONSE_HAVE_NO_WORDS.
    bool noSpeech = false;
    // Transport-level close events may be replayed once. A server task failure
    // means the request was parsed/rejected and must not be blindly replayed.
    bool retryable = false;
};

class TranscriptAccumulator {
public:
    void Apply(const Event& event);
    std::wstring Text() const;
    std::wstring CommittedText() const;
    bool HasCommittedText() const;

private:
    std::wstring committed_;
    std::wstring pending_;
};

// Pure protocol helpers are exposed so offline tests can validate the exact
// JSON/frame contract without opening a network connection.
std::string BuildRunTaskMessage(const Config& config, const std::string& taskId);
std::string BuildContinueTaskMessage(const std::string& taskId,
                                     const std::wstring& contextText);
std::string BuildFinishTaskMessage(const std::string& taskId);
Event ParseServerEventMessage(const std::string& message);

class Client {
public:
    explicit Client(Config config);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool Connect(std::wstring& error);
    bool SendAudio(const BYTE* data, size_t bytes, std::wstring& error);
    bool ContinueContext(const std::wstring& contextText, std::wstring& error);
    bool Poll(DWORD timeoutMs, Event& event, std::wstring& error);
    bool Finish(std::wstring& error);
    void Abort();
    void Close();
    // Mark a successfully finished task as idle.  The underlying WebSocket
    // remains open and may be used for a later run-task with the same
    // endpoint/model/API identity.
    bool PrepareForReuse();
    bool IsReusable() const;
    bool MatchesReuseIdentity(const Config& config) const;
    // Update task-specific parameters while the client is idle.  This keeps
    // vocabulary/language/context changes from being inherited by the next
    // recording while retaining the authenticated transport.
    bool ReconfigureForReuse(Config config);
    // Valid after Connect() returns false. This distinguishes a transient
    // transport/handshake failure from a server-side task rejection.
    bool LastFailureRetryable() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct TestResult { bool ok = false; std::wstring message; };
TestResult TestConnection(const Config& config);

// Audio 3 Streaming-only idle connection manager.  The manager never owns
// Qwen3 Realtime or HTTP clients; callers explicitly transfer a successfully
// finalized streaming client into and out of it.
std::unique_ptr<Client> AcquireReusableClient(const Config& config);
void ReleaseReusableClient(std::unique_ptr<Client> client);
void InvalidateReusableConnections();

} // namespace qwen_audio_streaming
