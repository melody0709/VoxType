#pragma once

#include <windows.h>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace browser_websocket {

inline constexpr unsigned short kPort = 19226;
inline constexpr std::string_view kPath = "/voxtype/browser/v2";
inline constexpr std::string_view kSubprotocol = "voxtype-context-v2";
inline constexpr size_t kMaxMessageBytes = 65536;

// The Windows WebSocket component owns framing, masking, UTF-8 and control frames.
// One event-driven I/O worker owns each handle; application sends only enqueue.
class Channel {
public:
    ~Channel();
    DWORD ProcessId() const;
    std::expected<void, std::string> Upgrade(std::string_view origin);
    bool Send(std::string message);
    std::expected<std::string, std::string> Receive(DWORD timeoutMs = INFINITE);
    void Close();

private:
    friend class Listener;
    struct Impl;
    explicit Channel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

class Listener {
public:
    Listener();
    ~Listener();
    std::expected<void, std::string> Start(unsigned short port = kPort);
    std::shared_ptr<Channel> Accept(HANDLE cancelled);
    void Close();
    unsigned short Port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace browser_websocket
