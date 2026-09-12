#pragma once

// The transport deliberately has no engine, SDL, GUI, or JSON dependencies. All
// socket ownership is confined to one worker; callers exchange bounded queues.
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Shipwright::Direct {
inline constexpr uint32_t ProtocolVersion = 1;
inline constexpr uint16_t DefaultPort = 43384;
inline constexpr size_t MaxPlayers = 8; // Includes the host.
inline constexpr size_t MaxFrameBytes = 512 * 1024;
inline constexpr size_t MaxQueueBytes = 4 * 1024 * 1024;

// A length-prefixed stream decoder, also exercised by the standalone tests.
class FrameDecoder {
  public:
    bool Feed(const char* bytes, size_t count, std::vector<std::string>& frames);
    void Clear();
  private:
    std::string pending;
};
std::string EncodeFrame(const std::string& payload);
bool IsIPv4(const std::string& address);

struct Event {
    enum class Type { Listening, Connected, Accepted, Message, Disconnected, Error } type;
    uint32_t peer = 0;
    std::string text;
};

class Transport final {
  public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    bool Host(uint16_t port);
    bool Join(const std::string& ipv4, uint16_t port);
    void Stop();
    bool Send(uint32_t peer, const std::string& payload);
    void Drop(uint32_t peer);
    std::vector<Event> Poll();
    bool Running() const { return running.load(); }
  private:
    struct Command {
        uint32_t peer;
        bool drop;
        std::string frame;
    };
    std::atomic<bool> running{ false };
    std::thread worker;
    std::mutex mutex;
    std::deque<Command> commands;
    std::deque<Event> events;
    size_t commandBytes = 0;
    size_t eventBytes = 0;
    bool Push(Event event);
    void Run(bool host, std::string address, uint16_t port);
};
} // namespace Shipwright::Direct
