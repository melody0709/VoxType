#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "asr_probe_service.h"

namespace asr_probe {

static IAsrProbeService* s_probeService = nullptr;

void RegisterProbeService(IAsrProbeService* service) {
    s_probeService = service;
}

IAsrProbeService* GetProbeService() {
    return s_probeService;
}

bool ValidateQwenVocabulary(const std::wstring& value, std::wstring* error) {
    if (s_probeService) {
        return s_probeService->ValidateVocabulary(value, error);
    }
    return true;
}

std::wstring GetQwenFreeStatusText(const std::wstring& utdidOverride, const std::wstring& shellPath) {
    if (s_probeService) {
        return s_probeService->GetQwenFreeStatus(utdidOverride, shellPath);
    }
    return L"";
}

void InvalidateQwenStreamingConnections() {
    if (s_probeService) {
        s_probeService->InvalidateStreamingConnections();
    }
}

} // namespace asr_probe
