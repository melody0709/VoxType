#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <expected>
#include <string>
#include <vector>

// Native Messaging host registration for the browser companion extension.
//
// Read path: strictly read-only. Chrome and Edge are classified independently,
// because one browser being ready must never stand in for the other, and any
// state that cannot be proven from the registry plus the file system is reported
// as its own state instead of being widened into "ready".
//
// Write path: the same contract as tools/register_browser_bridge.ps1. A
// registration this application did not create is never replaced, and what it
// overwrites is recorded first so the documented unregister path can restore it.
namespace browser_bridge_registration {

// Chromium resolves a native messaging host by exact key name under HKCU first,
// then HKLM, so a query that reads only HKCU can report "not registered" for a
// host the browser would in fact launch.
enum class HostState {
    NotRegistered,   // neither HKCU nor HKLM has the host key
    Ready,           // the key resolves to this instance's manifest, which exists
    PathMismatch,    // the key resolves to a different manifest that exists
    TargetMissing    // the key exists, but the manifest it names is not on disk
};

struct BridgeState {
    HostState chrome = HostState::NotRegistered;
    HostState edge = HostState::NotRegistered;
};

// One entry of %LOCALAPPDATA%\VoxType\browser-bridge-registration.json. The
// serialized shape is the PowerShell script's, and `previous` must be JSON null
// rather than an empty string, because the script branches on `$null -ne`.
struct Record {
    std::wstring key;
    bool existed = false;
    bool hasPrevious = false;
    std::wstring previous;
    std::wstring priorManifest; // Owned value before this attempt, for interrupted moves.
};

// <RuntimeAssetDir>\native-messaging\com.voxtype.browser_bridge.json, or an empty
// string when no runtime asset directory can be resolved.
std::wstring ManifestPath();

// The unpacked MV3 extension the user points "Load unpacked" at.
std::wstring ExtensionDir();

// Classifies one browser's host registration. Pure: it canonicalizes both paths
// (separators, "." / "..", 8.3 short names) and touches only the file system, so
// it is covered by the offline regression instead of by hand.
HostState ClassifyHost(const std::wstring& registeredManifestPath,
                       const std::wstring& localManifestPath);

// Reads HKCU then HKLM, 32-bit then 64-bit, for Chrome and Edge and classifies
// each one. Edge also accepts Chromium/Chrome fallback keys for older installs.
BridgeState QueryBridgeState();

// True when the manifest exists on disk and declares this host name, this
// executable as its `path`, and the stdio transport. Registering anything else
// would point the browser at a host it cannot launch.
bool IsHostManifestUsable(const std::wstring& manifestPath);

// The two canonical host key paths this feature owns and writes.
std::vector<std::wstring> HostKeyPaths();

// Registry lookup candidates. Chrome uses its own key; Edge checks its native
// key first, then Chromium and Chrome compatibility keys.
std::vector<std::wstring> HostLookupPaths(bool edge);

// The state file names the manifest the records belong to. That value is what
// lets a moved runtime directory be recognised as "ours" instead of being
// refused as an unknown third-party registration.
struct RegistrationState {
    std::wstring manifest;
    std::vector<Record> records;
};

// Codec for the registration state file. Exposed so the round-trip contract is
// covered by the offline regression rather than by hand.
RegistrationState ParseRegistrationState(const std::string& json);
std::string FormatRegistrationState(const std::vector<Record>& records,
                                    const std::wstring& manifestPath);

// Points the Chrome and Edge host keys at `manifestPath` (HKCU).
//
// Refuses, before writing either key, when the manifest is not usable or when a
// key already holds a value this application did not write. On success it records
// what it overwrote first, so tools\register_browser_bridge.ps1 -Action Unregister
// can still restore the previous state, including an interrupted directory move.
// The error carries the user-facing reason.
std::expected<void, std::wstring> RegisterHost(const std::wstring& manifestPath);

// The same contract against an explicit root key, host key paths and state
// directory. Exposed so the write path — the refusal rule and the recorded
// "before" values — is covered by the offline regression instead of only by
// hand: `RegisterHost` is the two-line wrapper that binds this to HKCU and to
// %LOCALAPPDATA%\VoxType. The state directory is created when necessary.
std::expected<void, std::wstring> RegisterHostAt(HKEY root,
                                                 const std::vector<std::wstring>& keyPaths,
                                                 const std::wstring& stateDirectory,
                                                 const std::wstring& manifestPath);

} // namespace browser_bridge_registration
