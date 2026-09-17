#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

bool EqualsIgnoreCase(std::wstring a, std::wstring b);
bool IsPortableMode();
std::wstring RuntimeAssetDir();
bool SetRuntimeAssetDirOverrideForProcess(const std::wstring& directory);
std::wstring MutableDataDir();
std::wstring DownloadedModelRoot();
std::wstring LogDir();
std::wstring AppDataDir();
std::wstring AppRootDir();
std::wstring ConfigPath();
std::wstring DefaultModelDir(const std::wstring& modelId);
bool ModelDirExists(const std::wstring& dir);
bool AnyModelDirExists();
bool RunModelDownloader(HWND hwnd, const std::wstring& modelId = L"firered_ctc");
std::wstring ModelDisplayName(const std::wstring& modelId);
int ModelIndex(const std::wstring& modelId);
std::wstring ModelIdFromIndex(int index);
void MigrateLegacyConfigIfNeeded();
bool ShouldFallbackFromLegacyModelDir(const std::wstring& modelDir);
