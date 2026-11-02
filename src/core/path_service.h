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
std::wstring VocabularyPath();
std::wstring DefaultModelDir(const std::wstring& modelId);
bool ModelDirExists(const std::wstring& dir);
bool AnyModelDirExists();
bool RunModelDownloader(HWND hwnd, const std::wstring& modelId = L"firered_ctc");
// 模型下载是跨窗口的长任务（PowerShell 进程 + 等待线程）。完成通知发给"当前"接收窗口，
// 而不是启动时捕获的句柄：Settings 窗口被重建（切换界面语言 / DPI）后旧句柄已销毁，
// 结果会静默丢失（模型目录字段不更新、无完成提示）。接收窗口在 Settings 创建/销毁时
// 登记，下载进行中时 IsModelDownloadRunning() 为真，重建出来的按钮必须据此保持禁用。
void SetModelDownloadNotifyWindow(HWND hwnd);
bool IsModelDownloadRunning();
std::wstring ModelDisplayName(const std::wstring& modelId);
int ModelIndex(const std::wstring& modelId);
std::wstring ModelIdFromIndex(int index);
void MigrateLegacyConfigIfNeeded();
bool ShouldFallbackFromLegacyModelDir(const std::wstring& modelDir);

namespace PathService {
inline std::wstring VocabularyPath() { return ::VocabularyPath(); }
inline std::wstring ConfigPath() { return ::ConfigPath(); }
inline std::wstring AppDataDir() { return ::AppDataDir(); }
inline std::wstring LogDir() { return ::LogDir(); }
}
