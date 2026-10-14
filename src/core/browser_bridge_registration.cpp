#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "browser_bridge_registration.h"

#include "browser_bridge.h"
#include "path_service.h"
#include "utils.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <format>
#include <string_view>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace browser_bridge_registration {

namespace {

// The first two keys are the ones this feature owns and writes. The additional
// Chromium/Chrome names are read-only Edge fallbacks used by older installations.
constexpr std::wstring_view kChromePrefix =
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\";
constexpr std::wstring_view kEdgePrefix =
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\";
constexpr std::wstring_view kChromiumPrefix =
    L"Software\\Chromium\\NativeMessagingHosts\\";

constexpr std::wstring_view kStateFileName = L"browser-bridge-registration.json";
constexpr std::wstring_view kManifestFileName = L"com.voxtype.browser_bridge.json";
constexpr std::wstring_view kManifestRelativePath = L"../VoxType.exe";

// A host registration may name a path of this many characters. The read path
// queries the size first instead of guessing, because a fixed buffer turns a
// long path into a silent "not registered".
constexpr DWORD kMaxValueChars = 32768;

std::wstring ChromeKeyPath() {
    return std::wstring(kChromePrefix) + browser_bridge::kHostName;
}

std::wstring EdgeKeyPath() {
    return std::wstring(kEdgePrefix) + browser_bridge::kHostName;
}

std::wstring ChromiumKeyPath() {
    return std::wstring(kChromiumPrefix) + browser_bridge::kHostName;
}

// Deliberately local: the settings tab carries its own copy for the startup
// registration errors, and a generic Win32 formatter does not belong in this
// feature's public surface just to save ten lines.
std::wstring DescribeWin32Error(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length && message ? std::wstring(message, length) : L"Unknown Windows error";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

bool PathsEqualIgnoreCase(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) return false;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}

// Resolves separators, "." and ".." so two spellings of one path compare equal,
// then resolves the long (non-8.3) form when the path exists, because
// GetFullPathNameW is purely syntactic and leaves "PROGRA~1" alone. Returns the
// input unchanged when nothing resolves, so a missing file compares predictably.
std::wstring CanonicalizeForCompare(const std::wstring& path) {
    if (path.empty()) return {};

    std::wstring resolved;
    for (DWORD capacity = MAX_PATH; capacity <= kMaxValueChars; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetFullPathNameW(path.c_str(), capacity, buffer.data(), nullptr);
        if (length == 0) return path;
        if (length < capacity) {
            resolved.assign(buffer.data(), length);
            break;
        }
    }
    if (resolved.empty()) return path;

    for (DWORD capacity = MAX_PATH; capacity <= kMaxValueChars; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetLongPathNameW(resolved.c_str(), buffer.data(), capacity);
        if (length == 0) break;
        if (length < capacity) return std::wstring(buffer.data(), length);
    }
    return resolved;
}

bool FileExistsOnDisk(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ExpandEnvironmentValue(const std::wstring& raw) {
    for (DWORD capacity = 512; capacity <= kMaxValueChars; capacity *= 2) {
        std::vector<wchar_t> expanded(capacity);
        const DWORD written = ExpandEnvironmentStringsW(raw.c_str(), expanded.data(), capacity);
        if (written == 0) return raw;
        if (written <= capacity) return std::wstring(expanded.data(), written - 1);
    }
    return raw;
}

std::expected<std::string, std::wstring> ReadTextFile(const std::wstring& path) {
    if (path.empty()) return std::unexpected(L"The file path is empty.");

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::unexpected(std::format(L"Cannot open {} ({})", path, GetLastError()));
    }

    std::string text;
    char buffer[4096];
    DWORD read = 0;
    BOOL ok = TRUE;
    while ((ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) && read > 0) {
        text.append(buffer, read);
    }
    CloseHandle(file);
    if (!ok) return std::unexpected(std::format(L"Cannot read {}", path));
    return text;
}

bool WriteTextFile(const std::wstring& path, const std::string& text) {
    const std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t offset = 0;
    while (offset < text.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(text.size() - offset, static_cast<size_t>(64 * 1024)));
        DWORD written = 0;
        if (!WriteFile(file, text.data() + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

// Reads a REG_SZ / REG_EXPAND_SZ default value. The size is queried first so an
// over-long value is reported as unreadable rather than silently becoming "no
// registration", and a value of another type never passes as a registration.
//
// `keyExists` is tracked separately from `value`, because the unregister path
// needs to know whether the *key* was there before us: the script deletes the
// subkey only when it created the key itself (`existed` false). Collapsing "key
// exists with no value" into "no key" would make it delete a key it did not own.
struct RegistryReadResult {
    bool keyExists = false;
    bool hasValue = false;
    std::wstring value;
    DWORD error = ERROR_SUCCESS;
};

RegistryReadResult ReadRegistryDefaultString(HKEY root, const std::wstring& subKey,
                                             REGSAM view = KEY_WOW64_64KEY) {
    RegistryReadResult result;

    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(root, subKey.c_str(), 0,
                                             KEY_READ | view, &key);
    if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND) return result;
    if (openStatus != ERROR_SUCCESS) {
        result.error = static_cast<DWORD>(openStatus);
        return result;
    }
    result.keyExists = true;

    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS probe = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size);
    if (probe == ERROR_FILE_NOT_FOUND) {
        // The key exists but holds no default value: that is "not registered",
        // not "registered somewhere else".
        RegCloseKey(key);
        return result;
    }
    if (probe != ERROR_SUCCESS) {
        result.error = static_cast<DWORD>(probe);
        RegCloseKey(key);
        return result;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        result.error = ERROR_INVALID_DATATYPE;
        RegCloseKey(key);
        return result;
    }
    if (size < sizeof(wchar_t) || size > kMaxValueChars * sizeof(wchar_t)) {
        result.error = ERROR_MORE_DATA;
        RegCloseKey(key);
        return result;
    }

    // The value is present from here on: an empty string is a value too, and the
    // script records it as `previous = ""`. Treating it as absent would lose it.
    result.hasValue = true;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    DWORD readSize = size;
    const LSTATUS read = RegQueryValueExW(key, nullptr, nullptr, &type,
                                          reinterpret_cast<LPBYTE>(buffer.data()), &readSize);
    if (read == ERROR_SUCCESS) {
        // Ignore anything past the first terminator: the value may omit it.
        buffer.back() = L'\0';
        result.value.assign(buffer.data());
        if (type == REG_EXPAND_SZ) result.value = ExpandEnvironmentValue(result.value);
    } else {
        result.error = static_cast<DWORD>(read);
    }
    RegCloseKey(key);
    return result;
}

struct HostLookupResult {
    bool found = false;
    RegistryReadResult registration;
};

HostLookupResult QueryHostManifestPath(const std::vector<std::wstring>& subKeys) {
    // Match the browser lookup boundary: user registration wins machine
    // registration, and a 32-bit entry wins the 64-bit entry in that root.
    constexpr REGSAM views[] = {KEY_WOW64_32KEY, KEY_WOW64_64KEY};
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (const REGSAM view : views) {
            for (const std::wstring& subKey : subKeys) {
                const RegistryReadResult result = ReadRegistryDefaultString(root, subKey, view);
                if (result.error != ERROR_SUCCESS || result.keyExists) {
                    return {true, result};
                }
            }
        }
    }
    return {};
}

// %LOCALAPPDATA%\VoxType — deliberately the same path the PowerShell script uses,
// so a registration made here is restorable by `-Action Unregister` and vice
// versa. MutableDataDir() diverges from it in Portable mode, and two state files
// would make each side refuse the other's registration.
std::wstring StateDirectory() {
    PWSTR known = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        base = known;
        CoTaskMemFree(known);
    }
    if (base.empty()) base = MutableDataDir();
    return base.empty() ? std::wstring() : base + L"\\VoxType";
}

// Creates the state directory if it is not there yet. Required in Portable mode:
// MutableDataDir() returns before its own EnsureDirectory() there, and this
// module deliberately uses %LOCALAPPDATA% instead of MutableDataDir() so the
// record stays where the PowerShell script looks for it. Without this the first
// click fails with ERROR_PATH_NOT_FOUND on a machine that only ran Portable.
bool EnsureStateDirectory(const std::wstring& directory) {
    if (directory.empty()) return false;
    if (CreateDirectoryW(directory.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace

std::vector<std::wstring> HostKeyPaths() {
    return {ChromeKeyPath(), EdgeKeyPath()};
}

std::vector<std::wstring> HostLookupPaths(bool edge) {
    if (!edge) return {ChromeKeyPath()};
    return {EdgeKeyPath(), ChromiumKeyPath(), ChromeKeyPath()};
}

std::wstring ManifestPath() {
    const std::wstring runtime = RuntimeAssetDir();
    if (runtime.empty()) return {};
    return runtime + L"\\native-messaging\\" + std::wstring(kManifestFileName);
}

std::wstring ExtensionDir() {
    const std::wstring runtime = RuntimeAssetDir();
    if (runtime.empty()) return {};
    return runtime + L"\\extensions\\browser-bridge";
}

HostState ClassifyHost(const std::wstring& registeredManifestPath,
                       const std::wstring& localManifestPath) {
    if (registeredManifestPath.empty()) return HostState::NotRegistered;

    const std::wstring local = CanonicalizeForCompare(localManifestPath);
    const std::wstring registered = CanonicalizeForCompare(registeredManifestPath);

    // "Ready" claims this instance is the one the browser will launch, so it
    // requires an identity match *and* a manifest that is actually on disk.
    if (!local.empty() && PathsEqualIgnoreCase(local, registered) && FileExistsOnDisk(local)) {
        return HostState::Ready;
    }
    if (FileExistsOnDisk(registeredManifestPath)) return HostState::PathMismatch;
    return HostState::TargetMissing;
}

BridgeState QueryBridgeState() {
    const std::wstring local = ManifestPath();
    BridgeState state;
    const HostLookupResult chrome = QueryHostManifestPath(HostLookupPaths(false));
    const HostLookupResult edge = QueryHostManifestPath(HostLookupPaths(true));
    state.chrome = chrome.found
        ? (chrome.registration.value.empty() ? HostState::TargetMissing
                                             : ClassifyHost(chrome.registration.value, local))
        : HostState::NotRegistered;
    state.edge = edge.found
        ? (edge.registration.value.empty() ? HostState::TargetMissing
                                           : ClassifyHost(edge.registration.value, local))
        : HostState::NotRegistered;
    return state;
}

bool IsHostManifestUsable(const std::wstring& manifestPath) {
    const auto text = ReadTextFile(manifestPath);
    if (!text) return false;

    // The manifest is UTF-8 JSON. A byte-order mark would make the first key
    // unparseable, so it is stripped rather than treated as a value.
    std::string json = *text;
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
        static_cast<unsigned char>(json[1]) == 0xBB &&
        static_cast<unsigned char>(json[2]) == 0xBF) {
        json.erase(0, 3);
    }

    if (ExtractJsonStringDecoded(json, "name") != browser_bridge::kHostName) return false;
    if (ExtractJsonStringDecoded(json, "type") != L"stdio") return false;
    return ExtractJsonStringDecoded(json, "path") == kManifestRelativePath;
}

RegistrationState ParseRegistrationState(const std::string& json) {
    RegistrationState state;
    if (!json_detail::IsValidDocument(json)) return state;

    state.manifest = ExtractJsonStringDecoded(json, "manifest");

    // PowerShell does not preserve hashtable field order; consume whole objects.
    const size_t pos = json_detail::FindValueForKey(json, "records");
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '[') return state;

    size_t cursor = pos + 1;
    while (cursor < json.size()) {
        json_detail::SkipWhitespace(json, cursor);
        if (cursor >= json.size()) break;
        if (json[cursor] == ']') break;
        if (json[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (json[cursor] != '{') return {};
        const size_t start = cursor;
        if (!json_detail::SkipValue(json, cursor)) return {};
        const std::string object = json.substr(start, cursor - start);
        Record record;
        record.key = ExtractJsonStringDecoded(object, "key");
        const size_t existed = json_detail::FindValueForKey(object, "existed");
        const size_t previous = json_detail::FindValueForKey(object, "previous");
        if (record.key.empty() || existed == std::string::npos || previous == std::string::npos ||
            (object.compare(existed, 4, "true") != 0 && object.compare(existed, 5, "false") != 0) ||
            (object[previous] != '"' && object.compare(previous, 4, "null") != 0)) return {};
        record.existed = object.compare(existed, 4, "true") == 0;
        record.hasPrevious = object[previous] == '"';
        if (record.hasPrevious) record.previous = DecodeJsonStringAt(object, previous);
        record.priorManifest = ExtractJsonStringDecoded(object, "priorManifest");
        state.records.push_back(std::move(record));
    }
    return state;
}

std::string FormatRegistrationState(const std::vector<Record>& records,
                                    const std::wstring& manifestPath) {
    std::string json = "{\n  \"records\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& record = records[i];
        json += "    {\n";
        json += std::format("      \"key\": \"{}\",\n", EscapeJson(record.key));
        json += std::format("      \"existed\": {},\n", record.existed ? "true" : "false");
        json += record.hasPrevious
                    ? std::format("      \"previous\": \"{}\"", EscapeJson(record.previous))
                    : std::string("      \"previous\": null");
        if (!record.priorManifest.empty()) {
            json += std::format(",\n      \"priorManifest\": \"{}\"", EscapeJson(record.priorManifest));
        }
        json += '\n';
        json += i + 1 == records.size() ? "    }\n" : "    },\n";
    }
    json += "  ],\n";
    json += std::format("  \"manifest\": \"{}\"\n", EscapeJson(manifestPath));
    json += "}\n";
    return json;
}

std::expected<void, std::wstring> RegisterHostAt(HKEY root,
                                                const std::vector<std::wstring>& keyPaths,
                                                const std::wstring& stateDirectory,
                                                const std::wstring& manifestPath) {
    if (!IsHostManifestUsable(manifestPath)) {
        return std::unexpected(std::format(
            L"The native messaging manifest is missing or does not declare this host:\n{}",
            manifestPath.empty() ? L"(no runtime asset directory)" : manifestPath));
    }
    if (stateDirectory.empty() || keyPaths.empty()) {
        return std::unexpected(L"The VoxType data directory could not be resolved.");
    }
    const std::wstring statePath = stateDirectory + L"\\" + std::wstring(kStateFileName);

    std::vector<RegistryReadResult> existing(keyPaths.size());
    for (size_t i = 0; i < keyPaths.size(); ++i) {
        existing[i] = ReadRegistryDefaultString(root, keyPaths[i]);
        if (existing[i].error != ERROR_SUCCESS) {
            return std::unexpected(std::format(
                L"Cannot read the current registration for {}\n({}): {}",
                keyPaths[i], existing[i].error, DescribeWin32Error(existing[i].error)));
        }
    }

    // Everything is decided before either key is written, mirroring the script's
    // preflight: a registration this application did not create is never
    // replaced; any interrupted writes retain enough information to resume.
    RegistrationState prior;
    if (FileExistsOnDisk(statePath)) {
        const auto priorText = ReadTextFile(statePath);
        if (!priorText) return std::unexpected(priorText.error());
        prior = ParseRegistrationState(*priorText);
        if (prior.manifest.empty() || prior.records.empty()) {
            return std::unexpected(std::format(L"The registration record is invalid; left it untouched:\n{}",
                                               statePath));
        }
    }

    std::vector<Record> records;
    for (size_t i = 0; i < keyPaths.size(); ++i) {
        const Record* saved = nullptr;
        for (const Record& candidate : prior.records) {
            if (PathsEqualIgnoreCase(candidate.key, keyPaths[i])) saved = &candidate;
        }

        Record record;
        record.key = keyPaths[i];
        if (saved) {
            // A record we already own is authoritative: it holds the state from
            // before this feature first touched the key, which is what Unregister
            // must restore. Overwriting it with the current (possibly our own)
            // value would make the script delete a key it did not create.
            record.existed = saved->existed;
            record.hasPrevious = saved->hasPrevious;
            record.previous = saved->previous;
        } else {
            record.existed = existing[i].keyExists;
            record.hasPrevious = existing[i].hasValue;
            record.previous = existing[i].value;
        }

        const std::wstring& current = existing[i].value;
        const bool pointsHere = PathsEqualIgnoreCase(current, manifestPath);
        // A value this application installed for a *different* runtime directory
        // is still ours, and that is exactly what the recorded manifest proves;
        // re-registering after a Portable move must not be refused.
        const bool savedForThisFeature = saved &&
            (PathsEqualIgnoreCase(current, prior.manifest) ||
             PathsEqualIgnoreCase(current, saved->priorManifest));
        // An empty value counts as a registration someone else made: the script's
        // own rule is `$null -ne $value`, and "" is not $null in PowerShell. The
        // empty value is still recorded as `previous` above, so refusing here
        // loses nothing and keeps the two paths on one contract.
        if (existing[i].hasValue && !pointsHere && !savedForThisFeature) {
            return std::unexpected(std::format(
                L"Another program already registered this browser host, so VoxType left it "
                 L"alone:\n{}\n\nIt points at:\n{}",
                keyPaths[i], current));
        }
        record.priorManifest = current;
        records.push_back(std::move(record));
    }

    // The record must exist before the keys change, so an interrupted run never
    // leaves a registration nobody can restore.
    const std::wstring directory = stateDirectory;
    if (!EnsureStateDirectory(directory)) {
        return std::unexpected(std::format(
            L"The registration record directory could not be created:\n{}", directory));
    }
    if (!WriteTextFile(statePath, FormatRegistrationState(records, manifestPath))) {
        return std::unexpected(std::format(
            L"The registration could not be recorded, so nothing was changed:\n{}", statePath));
    }

    for (const std::wstring& keyPath : keyPaths) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS created = RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr,
                                                REG_OPTION_NON_VOLATILE,
                                                KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
                                                &key, &disposition);
        if (created != ERROR_SUCCESS) {
            return std::unexpected(std::format(L"Cannot create {} ({}): {}",
                                               keyPath, created, DescribeWin32Error(created)));
        }
        const LSTATUS written = RegSetValueExW(
            key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(manifestPath.c_str()),
            static_cast<DWORD>((manifestPath.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (written != ERROR_SUCCESS) {
            return std::unexpected(std::format(L"Cannot set {} ({}): {}",
                                               keyPath, written, DescribeWin32Error(written)));
        }
    }
    return {};
}

std::expected<void, std::wstring> RegisterHost(const std::wstring& manifestPath) {
    const std::wstring directory = StateDirectory();
    if (directory.empty()) {
        return std::unexpected(L"The VoxType data directory could not be resolved.");
    }
    return RegisterHostAt(HKEY_CURRENT_USER, HostKeyPaths(), directory, manifestPath);
}

} // namespace browser_bridge_registration
