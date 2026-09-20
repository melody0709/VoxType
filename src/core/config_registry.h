#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "config_store.h"

namespace config_registry {

enum class CryptoPolicy {
    None,
    Dpapi,
};

enum class BoolJsonFormat {
    Literal,   // true / false
    QuotedInt, // "1" / "0"
    RawInt,    // 1 / 0
};

using MemberPtr = std::variant<
    std::wstring Config::*,
    bool Config::*,
    int Config::*,
    float Config::*
>;

struct FieldEntry {
    std::string_view jsonKey;
    MemberPtr member;
    CryptoPolicy crypto = CryptoPolicy::None;
    BoolJsonFormat boolFormat = BoolJsonFormat::Literal;
    int floatPrecision = -1;
    std::variant<std::monostate, std::wstring, bool, int, float> defaultValue{};
};

using LifecycleHook = std::function<void(Config&)>;

class Registry {
public:
    static Registry& Instance();

    void Register(FieldEntry entry);
    void SetPreSaveHook(LifecycleHook hook);
    void SetPostLoadHook(LifecycleHook hook);

    void LoadJson(Config& cfg, const std::string& json) const;
    std::string SaveJson(const Config& cfg) const;

    const std::vector<FieldEntry>& GetEntries() const { return m_entries; }
    void Clear();

private:
    std::vector<FieldEntry> m_entries;
    LifecycleHook m_preSaveHook;
    LifecycleHook m_postLoadHook;
};

void InitializeRegistry();

} // namespace config_registry
