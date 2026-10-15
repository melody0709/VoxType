#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "browser_bridge.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

namespace {

int failures = 0;
void Check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

bool WriteSocket(SOCKET socket, std::string_view data) {
    while (!data.empty()) {
        const int count = send(socket, data.data(), static_cast<int>(data.size()), 0);
        if (count <= 0) return false;
        data.remove_prefix(count);
    }
    return true;
}

std::string ReadServerMessage(SOCKET socket) {
    const auto read = [&](std::span<char> bytes) {
        while (!bytes.empty()) {
            const int count = recv(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
            if (count <= 0) return false;
            bytes = bytes.subspan(count);
        }
        return true;
    };
    unsigned char header[2]{};
    if (!read(std::span(reinterpret_cast<char*>(header), 2)) || header[0] != 0x81 || (header[1] & 0x80)) return {};
    uint64_t length = header[1] & 0x7f;
    if (length >= 126) {
        const size_t count = length == 126 ? 2 : 8;
        unsigned char extended[8]{};
        if (!read(std::span(reinterpret_cast<char*>(extended), count))) return {};
        length = 0;
        for (size_t i = 0; i < count; ++i) length = (length << 8) | extended[i];
    }
    if (length == 0 || length > 65536) return {};
    std::string message(static_cast<size_t>(length), '\0');
    return read(std::span(message.data(), message.size())) ? message : std::string{};
}

std::string ClientFrame(std::string_view text, unsigned char opcode = 1, bool masked = true, bool final = true) {
    std::string frame;
    frame += static_cast<char>((final ? 0x80 : 0) | opcode);
    const unsigned char mask = masked ? 0x80 : 0;
    if (text.size() < 126) frame += static_cast<char>(mask | text.size());
    else if (text.size() <= 65535) {
        frame += static_cast<char>(mask | 126);
        frame += static_cast<char>(text.size() >> 8);
        frame += static_cast<char>(text.size());
    } else {
        frame += static_cast<char>(mask | 127);
        for (int shift = 56; shift >= 0; shift -= 8) frame += static_cast<char>(static_cast<uint64_t>(text.size()) >> shift);
    }
    const unsigned char key[]{1, 2, 3, 4};
    if (masked) frame.append(reinterpret_cast<const char*>(key), sizeof(key));
    for (size_t i = 0; i < text.size(); ++i) frame += static_cast<char>(static_cast<unsigned char>(text[i]) ^ (masked ? key[i % 4] : 0));
    return frame;
}

struct ExchangeResult {
    bool upgraded = false;
    DWORD pid = 0;
    std::expected<std::string, std::string> received = std::unexpected("not_received");
    ULONGLONG cancelledMs = 0;
    bool authenticated = false;
};

enum class AuthAttack { None, WrongSecret, WrongVersion, UnknownProfile, Reflected, Replay };
struct AuthFixture {
    browser_pairing::Store* store = nullptr;
    browser_pairing::Credential credential;
    AuthAttack attack = AuthAttack::None;
    std::string savedProof;
};

void AuthenticateClient(SOCKET client, AuthFixture& fixture) {
    const auto challenge = ReadServerMessage(client);
    const auto nonce = WideToUtf8(ExtractJsonStringDecoded(challenge, "nonce"));
    const auto instance = WideToUtf8(ExtractJsonStringDecoded(challenge, "instance"));
    const auto clientNonce = browser_pairing::RandomHex(32);
    const auto profile = fixture.attack == AuthAttack::UnknownProfile ? std::string(32, '0') : fixture.credential.profile;
    WriteSocket(client, ClientFrame(std::format("{{\"version\":{},\"type\":\"auth_start\",\"profile\":\"{}\",\"nonce\":\"{}\"}}",
        fixture.attack == AuthAttack::WrongVersion ? 3 : 2, profile, clientNonce)));
    const auto serverProof = ReadServerMessage(client);
    if (serverProof.empty()) return;
    const auto transcript = instance + ":" + clientNonce + ":" + nonce;
    Check(browser_pairing::EqualProof(WideToUtf8(ExtractJsonStringDecoded(serverProof, "proof")),
                                     browser_pairing::Hmac(fixture.credential.secret, "server:" + transcript)),
          "the actual server proves its identity before accepting the client");
    std::string proof;
    if (fixture.attack == AuthAttack::Replay) proof = fixture.savedProof;
    else {
        const auto secret = fixture.attack == AuthAttack::WrongSecret ? std::string(64, '0') : fixture.credential.secret;
        proof = browser_pairing::Hmac(secret, (fixture.attack == AuthAttack::Reflected ? "server:" : "client:") + transcript);
        if (fixture.attack == AuthAttack::None) fixture.savedProof = proof;
    }
    WriteSocket(client, ClientFrame(std::format("{{\"version\":2,\"type\":\"auth_client\",\"proof\":\"{}\"}}", proof)));
    const auto accepted = ReadServerMessage(client);
    if (fixture.attack == AuthAttack::None) Check(ExtractJsonStringDecoded(accepted, "type") == L"auth_ok", "a valid client receives the authenticated acknowledgement");
    else Check(accepted.empty(), "an invalid client never receives an authenticated acknowledgement");
    if (!accepted.empty()) WriteSocket(client, ClientFrame("{\"ack\":true}"));
}

ExchangeResult Exchange(std::string headers, std::string frames, bool cancel = false, AuthFixture* auth = nullptr,
                        DWORD closeDelayMs = 20) {
    browser_websocket::Listener listener;
    if (!listener.Start(0)) { Check(false, "test listener binds loopback without elevation"); return {}; }
    browser_websocket::Listener collision;
    Check(!collision.Start(listener.Port()), "a listener cannot share or steal an occupied port");
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::promise<ExchangeResult> promise;
    auto result = promise.get_future();
    std::jthread server([&] {
        ExchangeResult output;
        auto channel = listener.Accept(stop);
        if (channel) {
            output.pid = channel->ProcessId();
            output.upgraded = channel->Upgrade("chrome-extension://cjmhlnmnolcbkjpfdbnpejfnigacbpai").has_value();
            if (output.upgraded) {
                if (auth) {
                    output.authenticated = browser_bridge::AuthenticateChannel(*channel, *auth->store, L"C:\\Browser\\chrome.exe").has_value();
                    if (output.authenticated) (void)channel->Receive(500);
                } else if (cancel) {
                    const auto start = GetTickCount64();
                    std::jthread closer([&] { Sleep(closeDelayMs); channel->Close(); });
                    output.received = channel->Receive(closeDelayMs == 20 ? INFINITE : 1000);
                    closer.join();
                    output.cancelledMs = GetTickCount64() - start;
                    Check(!channel->Receive(0), "a receive started after closure cannot wait or return a message");
                } else output.received = channel->Receive(1500);
            }
        }
        promise.set_value(std::move(output));
    });
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(listener.Port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    DWORD timeout = 2500;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    const bool connected = connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    Check(connected, "loopback test client connects");
    if (connected) {
        const auto request = std::format("GET /voxtype/browser/v2 HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n{}\r\n", listener.Port(), headers);
        WriteSocket(client, request);
        std::string response;
        char byte{};
        while (response.size() < 8192 && !response.ends_with("\r\n\r\n") && recv(client, &byte, 1, 0) == 1) response += byte;
        if (response.starts_with("HTTP/1.1 101")) {
            Check(response.find("Sec-WebSocket-Accept") != std::string::npos, "Windows constructs the server accept proof");
            if (!frames.empty()) WriteSocket(client, frames);
            if (auth) AuthenticateClient(client, *auth);
        }
    }
    const bool finished = result.wait_for(std::chrono::seconds(4)) == std::future_status::ready;
    Check(finished, "handshake, receive and cancellation remain bounded");
    if (!finished) shutdown(client, SD_BOTH);
    SetEvent(stop);
    closesocket(client);
    server.join();
    CloseHandle(stop);
    return result.get();
}

void TestTransport() {
    const std::string headers = "Origin: chrome-extension://cjmhlnmnolcbkjpfdbnpejfnigacbpai\r\nSec-WebSocket-Protocol: voxtype-context-v2\r\n";
    const auto normal = Exchange(headers, ClientFrame("{\"ok\":true}"));
    Check(normal.upgraded && normal.received && *normal.received == "{\"ok\":true}", "masked WebSocket text reaches the bounded message queue");
    Check(normal.pid == GetCurrentProcessId(), "TCP peer PID is resolved from the kernel table, not JSON");
    const auto fragmented = Exchange(headers, ClientFrame("{\"ok\":", 1, true, false) + ClientFrame("true}", 0));
    Check(fragmented.received && *fragmented.received == "{\"ok\":true}", "Windows reassembles a fragmented UTF-8 message");
    for (const auto& badHeaders : {
        std::string("Origin: https://example.test\r\nSec-WebSocket-Protocol: voxtype-context-v2\r\n"),
        std::string("Origin: chrome-extension://anotherextension\r\nSec-WebSocket-Protocol: voxtype-context-v2\r\n"),
        std::string("Origin: chrome-extension://cjmhlnmnolcbkjpfdbnpejfnigacbpai\r\nSec-WebSocket-Protocol: wrong-version\r\n"),
        headers + "Origin: https://example.test\r\n",
        headers + "Content-Length: 2\r\n"}) {
        Check(!Exchange(badHeaders, {}).upgraded, "web pages, unknown extensions, wrong protocols and ambiguous headers are rejected");
    }
    const std::string oversized = "{\"v\":\"" + std::string(65536, 'a') + "\"}";
    std::string deep(66, '[');
    deep += '0';
    deep.append(66, ']');
    for (const auto& invalid : {
        ClientFrame("{\"ok\":true}", 1, false), ClientFrame("{\"ok\":true}", 2),
        ClientFrame("not json"), ClientFrame(oversized), ClientFrame(deep),
        ClientFrame("{\"v\":\"\xff\"}"), ClientFrame("{\"ok\":true}", 0)}) {
        const auto malformed = Exchange(headers, invalid);
        Check(malformed.upgraded && !malformed.received, "malformed, unmasked, binary, oversized, deep and invalid UTF-8 messages fail closed");
    }
    const auto cancelled = Exchange(headers, {}, true);
    Check(cancelled.upgraded && !cancelled.received && cancelled.cancelledMs < 1000,
          "shutdown interrupts an idle receive and joins its I/O worker promptly");
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto raced = Exchange(headers, {}, true, nullptr, attempt % 2);
        Check(raced.upgraded && !raced.received && raced.cancelledMs < 500,
              "closure racing with receive entry cannot lose the wakeup");
    }
    Check(browser_bridge::BrowserRootPid(GetCurrentProcessId()) == 0,
          "an ordinary local process cannot impersonate the TCP browser owner");
}

void TestPairing() {
    using browser_pairing::Store;
    const auto secret = std::string(64, '0');
    Check(browser_pairing::Hmac(secret, "VoxType test") == "8b80c75bfa4b78c38ffd74c10a5ffe53be44cd79fc05040c43ba2a8202f38810",
          "CNG HMAC-SHA256 agrees with an independent known vector");
    Check(!browser_pairing::EqualProof({}, {}) && !browser_pairing::EqualProof(secret, std::string(64, '1')),
          "empty or incorrect proofs never authenticate");
    const auto nonce = browser_pairing::RandomHex(32);
    Check(browser_pairing::IsHex(nonce, 32) && nonce != browser_pairing::RandomHex(32), "fresh nonces use the operating-system cryptographic RNG");
    const auto proof = browser_pairing::Hmac(secret, "server:instance:client:server");
    Check(!browser_pairing::EqualProof(proof, browser_pairing::Hmac(secret, "client:instance:client:server")) &&
          !browser_pairing::EqualProof(proof, browser_pairing::Hmac(secret, "server:instance:client:other")),
          "role separation and fresh challenges prevent reflected or replayed proofs");
    const auto directory = std::filesystem::temp_directory_path() / std::format(L"VoxTypePairingTest-{}-{}", GetCurrentProcessId(), Utf8ToWide(browser_pairing::RandomHex(8)));
    const auto path = (directory / L"pairing.dat").wstring();
    Store store(path);
    Check(store.Load().has_value() && store.Count() == 0, "a fresh store begins unpaired");
    const auto instance = store.InstanceId();
    const auto profile = browser_pairing::RandomHex(16);
    const auto paired = store.Pair(profile, L"C:\\Browser\\chrome.exe");
    Check(paired && browser_pairing::IsHex(paired->secret, 32), "pairing creates a per-profile strong credential");
    if (!paired) return;
    const auto encrypted = [&] { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); }();
    Check(!encrypted.empty() && encrypted.find(paired->secret) == std::string::npos, "credentials are encrypted on disk rather than JSON plaintext");
    Store restarted(path);
    Check(restarted.Load().has_value() && restarted.InstanceId() == instance && restarted.Find(profile, L"C:\\Browser\\chrome.exe").has_value(), "pairing survives a desktop restart");
    Check(!restarted.Find(profile, L"C:\\Browser\\msedge.exe"), "a Chrome pairing cannot authenticate another browser image");
    Check(restarted.CountForBrowser(L"chrome.exe") == 1 && restarted.CountForBrowser(L"msedge.exe") == 0,
          "paired browser states remain independent");
    const auto replacement = restarted.Pair(profile, L"C:\\Browser\\chrome.exe");
    Check(replacement && replacement->secret != paired->secret && restarted.Count() == 1, "re-pairing rotates only that profile's old credential");
    const std::string headers = "Origin: chrome-extension://cjmhlnmnolcbkjpfdbnpejfnigacbpai\r\nSec-WebSocket-Protocol: voxtype-context-v2\r\n";
    AuthFixture fixture{&restarted, *replacement};
    Check(Exchange(headers, {}, false, &fixture).authenticated, "real WebSocket mutual authentication accepts the paired profile");
    for (const auto attack : {AuthAttack::WrongSecret, AuthAttack::WrongVersion, AuthAttack::UnknownProfile, AuthAttack::Reflected, AuthAttack::Replay}) {
        fixture.attack = attack;
        Check(!Exchange(headers, {}, false, &fixture).authenticated, "wrong credentials, versions, profiles, reflected and replayed proofs fail authentication");
    }
    const auto otherProfile = browser_pairing::RandomHex(16);
    Check(restarted.Pair(otherProfile, L"C:\\Browser\\msedge.exe").has_value() && restarted.Count() == 2, "Chrome and Edge profiles hold independent credentials");
    Check(restarted.RevokeAll().has_value() && restarted.Count() == 0 && restarted.InstanceId() != instance &&
          !restarted.Find(profile, L"C:\\Browser\\chrome.exe"), "explicit revocation invalidates every credential and the installation challenge identity");
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "corrupt";
    }
    Store damaged(path);
    Check(!damaged.Load() && !damaged.Pair(profile, L"C:\\Browser\\chrome.exe") && damaged.Count() == 0,
          "a corrupt store is never silently replaced during pairing");
    Check(damaged.RevokeAll().has_value() && damaged.Pair(profile, L"C:\\Browser\\chrome.exe").has_value(),
          "explicit revocation recovers a corrupt credential store");
    Store moved((directory / L"moved.dat").wstring());
    Check(moved.Load().has_value() && moved.Count() == 0 && moved.InstanceId() != instance,
          "another installation directory starts with a different identity and needs pairing");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
}

} // namespace

int TestBrowserWebSocketContracts() {
    TestPairing();
    TestTransport();
    return failures;
}
