#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <functional>
#include <string>

#include "config_store.h"

namespace asr_probe {

struct DoubaoCredentials {
    std::wstring deviceId;
    std::wstring cdid;
    std::wstring token;
};

struct ProbeRequest {
    std::wstring provider;
    Config configSnapshot;
};

struct ProbeResult {
    bool ok = false;
    std::wstring message;
    bool credentialsChanged = false;
    DoubaoCredentials credentials;
    bool asrOk = false;
    bool llmOk = false;
};

using ProbeCallback = std::function<void(const ProbeResult&)>;

class IAsrProbeService {
public:
    virtual ~IAsrProbeService() = default;
    virtual void ProbeAsync(const ProbeRequest& req, ProbeCallback callback) = 0;
    virtual bool ValidateVocabulary(const std::wstring& value, std::wstring* error = nullptr) = 0;
    virtual std::wstring GetQwenFreeStatus(const std::wstring& utdidOverride, const std::wstring& shellPath) = 0;
    virtual void InvalidateStreamingConnections() = 0;
};

void RegisterProbeService(IAsrProbeService* service);
IAsrProbeService* GetProbeService();

// Decoupled helpers to eliminate cross-layer includes from UI:
bool ValidateQwenVocabulary(const std::wstring& value, std::wstring* error = nullptr);
std::wstring GetQwenFreeStatusText(const std::wstring& utdidOverride, const std::wstring& shellPath);
void InvalidateQwenStreamingConnections();

// Audio diagnostics helpers decoupled from UI:
std::wstring NormalizeDiagnosticAudioMode(const std::wstring& mode);
std::wstring GetDiagnosticAudioDir();
bool EnsureDiagnosticAudioDir(std::wstring* error = nullptr);
bool DeleteDiagnosticManagedRecordings(size_t* deletedGroups = nullptr, std::wstring* error = nullptr);

} // namespace asr_probe
