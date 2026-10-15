#pragma once

#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace browser_pairing {

inline constexpr size_t kMaxPairings = 32;

bool IsHex(std::string_view value, size_t bytes);
std::string RandomHex(size_t bytes);
std::string Hmac(std::string_view secret, std::string_view message);
bool EqualProof(std::string_view expected, std::string_view supplied);
std::wstring DefaultStorePath();

struct Credential {
    std::string profile;
    std::string secret;
    std::wstring browserImage;
};

// These are transport credentials, not user preferences in Config.
// A damaged store fails closed until the user explicitly revokes it.
class Store {
public:
    explicit Store(std::wstring path = DefaultStorePath());
    std::expected<void, std::string> Load();
    std::expected<Credential, std::string> Pair(std::string profile, std::wstring browserImage);
    std::optional<Credential> Find(std::string_view profile, std::wstring_view browserImage) const;
    std::expected<void, std::string> RevokeAll();
    std::string InstanceId() const;
    size_t Count() const;
    size_t CountForBrowser(std::wstring_view basename) const;
    std::string Error() const;

private:
    std::expected<void, std::string> Save(const std::vector<Credential>& credentials,
                                         const std::string& instance);
    std::wstring path_;
    mutable std::mutex mutex_;
    std::string instance_;
    std::string error_;
    std::vector<Credential> credentials_;
};

} // namespace browser_pairing
