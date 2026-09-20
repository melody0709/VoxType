#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "path_service.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr wchar_t kPortableFlagName[] = L"portable.flag";
std::mutex g_runtimeAssetDirMutex;
std::wstring g_runtimeAssetDirOverride;

std::wstring CurrentExecutableDirectory() {
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0) break;
        if (length < capacity - 1) {
            std::wstring path(buffer.data(), length);
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
        }
    }
    return {};
}

bool PathExistsAsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool PathStartsWithIgnoreCase(const std::wstring& path, const std::wstring& prefix) {
    if (path.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (towlower(path[i]) != towlower(prefix[i])) return false;
    }
    return path.size() == prefix.size() ||
        prefix.empty() ||
        prefix.back() == L'\\' ||
        prefix.back() == L'/' ||
        path[prefix.size()] == L'\\' ||
        path[prefix.size()] == L'/';
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring DirectoryLeafName(const std::wstring& path) {
    const size_t end = path.find_last_not_of(L"\\/");
    if (end == std::wstring::npos) return {};
    const size_t slash = path.find_last_of(L"\\/", end);
    const size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    return path.substr(start, end - start + 1);
}

std::wstring DevelopmentRootConfigPath() {
    const std::wstring runtimeDir = RuntimeAssetDir();
    const std::wstring runDir = ParentDirectory(runtimeDir);
    const std::wstring buildDir = ParentDirectory(runDir);
    if (!EqualsIgnoreCase(DirectoryLeafName(runtimeDir), L"x64-release") ||
        !EqualsIgnoreCase(DirectoryLeafName(runDir), L"run") ||
        !EqualsIgnoreCase(DirectoryLeafName(buildDir), L"build")) {
        return {};
    }

    const std::wstring repositoryRoot = ParentDirectory(buildDir);
    return repositoryRoot.empty() ? std::wstring() : repositoryRoot + L"\\config.json";
}

void EnsureDirectory(const std::wstring& path) {
    if (!path.empty()) CreateDirectoryW(path.c_str(), nullptr);
}

} // namespace

bool EqualsIgnoreCase(std::wstring a, std::wstring b) {
    std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return a == b;
}

std::wstring AppRootDir() {
    return CurrentExecutableDirectory();
}

std::wstring RuntimeAssetDir() {
    std::lock_guard<std::mutex> lock(g_runtimeAssetDirMutex);
    return g_runtimeAssetDirOverride.empty()
        ? AppRootDir()
        : g_runtimeAssetDirOverride;
}

bool SetRuntimeAssetDirOverrideForProcess(const std::wstring& directory) {
    if (directory.empty()) {
        std::lock_guard<std::mutex> lock(g_runtimeAssetDirMutex);
        g_runtimeAssetDirOverride.clear();
        return true;
    }

    const DWORD required = GetFullPathNameW(directory.c_str(), 0, nullptr, nullptr);
    if (required == 0) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1, L'\0');
    const DWORD length = GetFullPathNameW(
        directory.c_str(), static_cast<DWORD>(buffer.size()),
        buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return false;

    std::wstring resolved(buffer.data(), length);
    while (resolved.size() > 3 &&
           (resolved.back() == L'\\' || resolved.back() == L'/')) {
        resolved.pop_back();
    }
    const DWORD attributes = GetFileAttributesW(resolved.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_runtimeAssetDirMutex);
    g_runtimeAssetDirOverride = std::move(resolved);
    return true;
}

bool IsPortableMode() {
    return PathExistsAsFile(RuntimeAssetDir() + L"\\" + kPortableFlagName);
}

std::wstring MutableDataDir() {
    if (IsPortableMode()) {
        return RuntimeAssetDir();
    }

    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = path;
        CoTaskMemFree(path);
    }
    if (result.empty()) {
        return RuntimeAssetDir();
    }

    result += L"\\VoxType";
    EnsureDirectory(result);
    return result;
}

std::wstring AppDataDir() {
    return MutableDataDir();
}

std::wstring DownloadedModelRoot() {
    return MutableDataDir() + L"\\models";
}

std::wstring LogDir() {
    const std::wstring result = MutableDataDir() + L"\\log";
    EnsureDirectory(result);
    return result;
}

std::wstring ConfigPath() {
    return MutableDataDir() + L"\\config.json";
}

std::wstring DefaultModelDir(const std::wstring& modelId) {
    const std::wstring base = DownloadedModelRoot() + L"\\";
    if (modelId == L"firered_aed") {
        return base + L"sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26";
    }
    if (modelId == L"sensevoice") {
        return base + L"sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17";
    }
    return base + L"sherpa-onnx-fire-red-asr2-ctc-zh_en-int8-2026-02-25";
}

bool ModelDirExists(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attr = GetFileAttributesW(dir.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool AnyModelDirExists() {
    const std::wstring base = DownloadedModelRoot() + L"\\";
    const std::wstring dirs[] = {
        L"sherpa-onnx-fire-red-asr2-ctc-zh_en-int8-2026-02-25",
        L"sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26",
        L"sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"
    };
    for (const auto& d : dirs) {
        if (ModelDirExists(base + d)) return true;
    }
    return false;
}

bool RunModelDownloader(HWND hwnd, const std::wstring& modelId) {
    const std::wstring scriptPath = RuntimeAssetDir() + L"\\download_models.ps1";

    if (GetFileAttributesW(scriptPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd,
            L"download_models.ps1 not found.\n\n"
            L"Please download models manually from:\n"
            L"https://github.com/k2-fsa/sherpa-onnx/releases",
            L"Script Not Found", MB_OK | MB_ICONWARNING);
        return false;
    }

    const std::wstring aria2Path = RuntimeAssetDir() + L"\\aria2c.exe";
    const std::wstring cmd = L"-ExecutionPolicy Bypass -NoExit -File \"" + scriptPath +
        L"\" -Destination \"" + DownloadedModelRoot() +
        L"\" -Aria2Path \"" + aria2Path + L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = nullptr;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = cmd.c_str();
    const std::wstring workingDirectory = RuntimeAssetDir();
    sei.lpDirectory = workingDirectory.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(hwnd, L"Failed to start download.", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    if (sei.hProcess) {
        std::thread([hProcess = sei.hProcess, hwnd, modelId]() {
            WaitForSingleObject(hProcess, INFINITE);
            CloseHandle(hProcess);
            std::wstring modelDir = DefaultModelDir(modelId);
            LPARAM lParam = ModelDirExists(modelDir)
                ? reinterpret_cast<LPARAM>(new std::wstring(std::move(modelDir)))
                : 0;
            if (!PostMessageW(hwnd, WM_APP + 20, 0, lParam)) {
                if (lParam) delete reinterpret_cast<std::wstring*>(lParam);
            }
        }).detach();
    } else {
        PostMessageW(hwnd, WM_APP + 20, 0, 0);
    }

    return true;
}

std::wstring ModelDisplayName(const std::wstring& modelId) {
    if (modelId == L"firered_aed") return L"FireRedASR2 AED";
    if (modelId == L"sensevoice") return L"SenseVoiceSmall";
    if (modelId == L"baidu") return L"Baidu Cloud";
    if (modelId == L"volcengine") return L"Volcano Engine";
    if (modelId == L"mimo") return L"MiMo ASR";
    if (modelId == L"doubao_ime") return L"Doubao IME";
    return L"FireRedASR2 CTC";
}

int ModelIndex(const std::wstring& modelId) {
    if (modelId == L"firered_aed") return 1;
    if (modelId == L"sensevoice") return 2;
    return 0;
}

std::wstring ModelIdFromIndex(int index) {
    if (index == 1) return L"firered_aed";
    if (index == 2) return L"sensevoice";
    return L"firered_ctc";
}

void MigrateLegacyConfigIfNeeded() {
    if (IsPortableMode()) return;

    const std::wstring destination = ConfigPath();
    if (PathExistsAsFile(destination)) return;

    const std::wstring adjacentConfig = RuntimeAssetDir() + L"\\config.json";
    if (PathExistsAsFile(adjacentConfig) && CopyFileW(adjacentConfig.c_str(), destination.c_str(), TRUE)) {
        return;
    }

    const std::wstring developmentConfig = DevelopmentRootConfigPath();
    if (!developmentConfig.empty() && PathExistsAsFile(developmentConfig)) {
        CopyFileW(developmentConfig.c_str(), destination.c_str(), TRUE);
    }
}

bool ShouldFallbackFromLegacyModelDir(const std::wstring& modelDir) {
    if (modelDir.empty() || !ModelDirExists(modelDir)) return true;

    return !IsPortableMode() &&
        PathStartsWithIgnoreCase(modelDir, RuntimeAssetDir() + L"\\models");
}
