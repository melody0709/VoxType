#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "browser_pairing.h"
#include "utils.h"

#include <bcrypt.h>
#include <wincrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <span>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace browser_pairing {
namespace {

std::string Hex(std::span<const unsigned char> bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        out += "0123456789abcdef"[byte >> 4];
        out += "0123456789abcdef"[byte & 15];
    }
    return out;
}

std::string Sha256(std::string_view input) {
    unsigned char hash[32]{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                   static_cast<ULONG>(input.size()), hash, sizeof(hash)) < 0) return {};
    return Hex(hash);
}

std::wstring UserSddl() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    const bool read = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
    CloseHandle(token);
    wchar_t* sid = nullptr;
    if (!read || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return {};
    auto result = std::format(L"D:P(A;;GA;;;{})", sid);
    LocalFree(sid);
    return result;
}

} // namespace

bool IsHex(std::string_view value, size_t bytes) {
    return value.size() == bytes * 2 && std::ranges::all_of(value, [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

std::string RandomHex(size_t bytes) {
    if (bytes == 0 || bytes > 64) return {};
    std::vector<unsigned char> buffer(bytes);
    if (BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
    return Hex(buffer);
}

std::string Hmac(std::string_view secret, std::string_view message) {
    if (!IsHex(secret, 32) || message.size() > 1024) return {};
    unsigned char key[32]{};
    const auto digit = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<unsigned char>(digit(secret[i * 2]) * 16 + digit(secret[i * 2 + 1]));
    unsigned char hash[32]{};
    const auto status = BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, key, sizeof(key),
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
                                   static_cast<ULONG>(message.size()), hash, sizeof(hash));
    SecureZeroMemory(key, sizeof(key));
    return status < 0 ? std::string{} : Hex(hash);
}

bool EqualProof(std::string_view expected, std::string_view supplied) {
    if (!IsHex(expected, 32) || !IsHex(supplied, 32)) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) difference |= expected[i] ^ supplied[i];
    return difference == 0;
}

std::wstring DefaultStorePath() {
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    const std::filesystem::path directory = std::filesystem::path(local) / L"VoxType";
    CoTaskMemFree(local);
    std::wstring image(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (!length || length >= image.size()) return {};
    image.resize(length);
    std::ranges::transform(image, image.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    const auto hash = Sha256(WideToUtf8(image));
    if (hash.empty()) return {};
    return (directory / std::format(L"browser-websocket-pairing-{}.dat", Utf8ToWide(hash.substr(0, 16)))).wstring();
}

Store::Store(std::wstring path) : path_(std::move(path)) {}

std::expected<void, std::string> Store::Load() {
    std::lock_guard lock(mutex_);
    credentials_.clear();
    instance_.clear();
    error_.clear();
    if (path_.empty()) return std::unexpected(error_ = "pairing_path_unavailable");
    HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return std::unexpected(error_ = "pairing_store_unreadable");
        instance_ = RandomHex(16);
        if (instance_.empty()) return std::unexpected(error_ = "random_unavailable");
        return {};
    }
    LARGE_INTEGER size{};
    const bool sized = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 65536;
    std::vector<unsigned char> encrypted(sized ? static_cast<size_t>(size.QuadPart) : 0);
    DWORD read = 0;
    const bool readOk = sized && ReadFile(file, encrypted.data(), static_cast<DWORD>(encrypted.size()), &read, nullptr) && read == encrypted.size();
    CloseHandle(file);
    if (!readOk) return std::unexpected(error_ = "pairing_store_invalid");
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::unexpected(error_ = "pairing_store_invalid");
    }
    std::string json(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    const auto instance = WideToUtf8(ExtractJsonStringDecoded(json, "instance"));
    if (!json_detail::IsValidDocument(json) || !IsHex(instance, 16)) return std::unexpected(error_ = "pairing_store_invalid");
    const size_t start = json_detail::FindValueForKey(json, "credentials");
    if (start == std::string::npos || json[start] != '[') return std::unexpected(error_ = "pairing_store_invalid");
    size_t pos = start + 1;
    while (pos < json.size()) {
        while (pos < json.size() && (isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) ++pos;
        if (pos < json.size() && json[pos] == ']') break;
        if (pos >= json.size() || json[pos] != '{' || credentials_.size() >= kMaxPairings) return std::unexpected(error_ = "pairing_store_invalid");
        size_t end = pos;
        if (!json_detail::SkipValue(json, end)) return std::unexpected(error_ = "pairing_store_invalid");
        const auto object = json.substr(pos, end - pos);
        Credential credential{WideToUtf8(ExtractJsonStringDecoded(object, "profile")),
                              WideToUtf8(ExtractJsonStringDecoded(object, "secret")),
                              ExtractJsonStringDecoded(object, "browser")};
        if (!IsHex(credential.profile, 16) || !IsHex(credential.secret, 32) || credential.browserImage.empty() ||
            std::ranges::any_of(credentials_, [&](const auto& old) { return old.profile == credential.profile; })) {
            return std::unexpected(error_ = "pairing_store_invalid");
        }
        credentials_.push_back(std::move(credential));
        pos = end;
    }
    instance_ = instance;
    return {};
}

std::expected<void, std::string> Store::Save(const std::vector<Credential>& credentials, const std::string& instance) {
    if (path_.empty() || !IsHex(instance, 16)) return std::unexpected("pairing_path_unavailable");
    std::string json = std::format("{{\"instance\":\"{}\",\"credentials\":[", instance);
    for (const auto& entry : credentials) {
        if (json.back() != '[') json += ',';
        json += std::format("{{\"profile\":\"{}\",\"secret\":\"{}\",\"browser\":\"{}\"}}",
                            entry.profile, entry.secret, EscapeJson(entry.browserImage));
    }
    json += "]}";
    if (json.size() > 60000) return std::unexpected("pairing_store_full");
    DATA_BLOB input{static_cast<DWORD>(json.size()), reinterpret_cast<BYTE*>(json.data())}, output{};
    if (!CryptProtectData(&input, L"VoxType browser pairing", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return std::unexpected("pairing_encrypt_failed");
    SecureZeroMemory(json.data(), json.size());
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), error);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto sddl = UserSddl();
    if (error || sddl.empty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        LocalFree(output.pbData);
        return std::unexpected("pairing_save_failed");
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const auto temporary = path_ + L"." + Utf8ToWide(RandomHex(8)) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    DWORD written = 0;
    const bool saved = file != INVALID_HANDLE_VALUE && WriteFile(file, output.pbData, output.cbData, &written, nullptr) && written == output.cbData && FlushFileBuffers(file);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return std::unexpected("pairing_save_failed");
    }
    return {};
}

std::expected<Credential, std::string> Store::Pair(std::string profile, std::wstring browserImage) {
    std::lock_guard lock(mutex_);
    if (!error_.empty()) return std::unexpected(error_);
    if (!IsHex(profile, 16) || browserImage.empty()) return std::unexpected("invalid_pairing");
    auto next = credentials_;
    std::erase_if(next, [&](const auto& old) { return old.profile == profile; });
    if (next.size() >= kMaxPairings) return std::unexpected("pairing_limit");
    Credential credential{std::move(profile), RandomHex(32), std::move(browserImage)};
    if (credential.secret.empty()) return std::unexpected("random_unavailable");
    next.push_back(credential);
    const auto saved = Save(next, instance_);
    if (!saved) return std::unexpected(saved.error());
    credentials_ = std::move(next);
    return credential;
}

std::optional<Credential> Store::Find(std::string_view profile, std::wstring_view browserImage) const {
    std::lock_guard lock(mutex_);
    if (!error_.empty()) return std::nullopt;
    const auto found = std::ranges::find_if(credentials_, [&](const auto& entry) {
        return entry.profile == profile && entry.browserImage == browserImage;
    });
    return found == credentials_.end() ? std::nullopt : std::optional(*found);
}

std::expected<void, std::string> Store::RevokeAll() {
    std::lock_guard lock(mutex_);
    const auto instance = RandomHex(16);
    if (instance.empty()) return std::unexpected("random_unavailable");
    const auto saved = Save({}, instance);
    if (!saved) return saved;
    credentials_.clear();
    instance_ = instance;
    error_.clear();
    return {};
}

std::string Store::InstanceId() const { std::lock_guard lock(mutex_); return instance_; }
size_t Store::Count() const { std::lock_guard lock(mutex_); return error_.empty() ? credentials_.size() : 0; }
size_t Store::CountForBrowser(std::wstring_view basename) const {
    std::lock_guard lock(mutex_);
    if (!error_.empty()) return 0;
    return std::ranges::count_if(credentials_, [&](const auto& entry) {
        return _wcsicmp(std::filesystem::path(entry.browserImage).filename().c_str(), std::wstring(basename).c_str()) == 0;
    });
}
std::string Store::Error() const { std::lock_guard lock(mutex_); return error_; }

} // namespace browser_pairing
