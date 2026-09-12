#include "Transport.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

#if defined(__SWITCH__) || defined(__WIIU__)
#define SHIP_DIRECT_UNSUPPORTED
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Shipwright::Direct {
bool IsIPv4(const std::string& address) {
    if (address.empty() || address.size() > 15) return false;
    unsigned segments = 0, value = 0, digits = 0;
    uint32_t ip = 0;
    for (char c : address + '.') {
        if (c == '.') {
            if (digits == 0 || value > 255 || ++segments > 4) return false;
            ip = (ip << 8) | value;
            value = digits = 0;
        } else {
            if (c < '0' || c > '9' || ++digits > 3) return false;
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
    }
    return segments == 4 && ip != 0 && ip != UINT32_MAX;
}
std::string EncodeFrame(const std::string& payload) {
    if (payload.empty() || payload.size() > MaxFrameBytes) throw std::length_error("Invalid network frame size");
    uint32_t n = static_cast<uint32_t>(payload.size());
    std::string frame(4, '\0');
    for (unsigned i = 0; i < 4; ++i) frame[i] = static_cast<char>((n >> ((3 - i) * 8)) & 255);
    frame += payload;
    return frame;
}
bool FrameDecoder::Feed(const char* bytes, size_t count, std::vector<std::string>& frames) {
    // The transport passes bounded reads. The extra frame allowance permits a
    // completed large frame and the start of the next one in a single recv.
    if (count > MaxFrameBytes + 4 || pending.size() + count > 2 * (MaxFrameBytes + 4)) return false;
    pending.append(bytes, count);
    size_t offset = 0;
    while (pending.size() - offset >= 4) {
        uint32_t length = 0;
        for (unsigned i = 0; i < 4; ++i) length = (length << 8) | static_cast<unsigned char>(pending[offset + i]);
        if (length == 0 || length > MaxFrameBytes) return false;
        if (pending.size() - offset - 4 < length) break;
        frames.emplace_back(pending.data() + offset + 4, length);
        offset += 4 + length;
    }
    pending.erase(0, offset);
    return true;
}
void FrameDecoder::Clear() { pending.clear(); }
Transport::Transport() = default;
Transport::~Transport() { Stop(); }

bool Transport::Host(uint16_t port) {
    if (running || port == 0) return false;
    Stop();
    running = true;
    worker = std::thread(&Transport::Run, this, true, std::string{}, port);
    return true;
}
bool Transport::Join(const std::string& ipv4, uint16_t port) {
    if (running || !IsIPv4(ipv4) || port == 0) return false;
    Stop();
    running = true;
    worker = std::thread(&Transport::Run, this, false, ipv4, port);
    return true;
}
void Transport::Stop() {
    running = false;
    if (worker.joinable()) worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    commands.clear();
    events.clear();
    commandBytes = eventBytes = 0;
}
bool Transport::Send(uint32_t peer, const std::string& payload) {
    if (!running || payload.empty() || payload.size() > MaxFrameBytes) return false;
    auto frame = EncodeFrame(payload);
    std::lock_guard<std::mutex> lock(mutex);
    if (commands.size() >= 4096 || commandBytes + frame.size() > MaxQueueBytes) return false;
    commandBytes += frame.size();
    commands.push_back({ peer, false, std::move(frame) });
    return true;
}
void Transport::Drop(uint32_t peer) {
    std::lock_guard<std::mutex> lock(mutex);
    if (commands.size() < 4096) commands.push_back({ peer, true, {} });
}
std::vector<Event> Transport::Poll() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Event> result;
    result.reserve(events.size());
    while (!events.empty()) {
        result.push_back(std::move(events.front()));
        events.pop_front();
    }
    eventBytes = 0;
    return result;
}
bool Transport::Push(Event event) {
    std::lock_guard<std::mutex> lock(mutex);
    if (events.size() >= 4096 || eventBytes + event.text.size() > MaxQueueBytes) return false;
    eventBytes += event.text.size();
    events.push_back(std::move(event));
    return true;
}

#ifndef SHIP_DIRECT_UNSUPPORTED
namespace {
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Invalid = INVALID_SOCKET;
void Close(Socket socket) { if (socket != Invalid) closesocket(socket); }
bool WouldBlock() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY || e == WSAEINTR;
}
bool NonBlocking(Socket socket) { u_long on = 1; return ioctlsocket(socket, FIONBIO, &on) == 0; }
#else
using Socket = int;
constexpr Socket Invalid = -1;
void Close(Socket socket) { if (socket != Invalid) close(socket); }
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
bool NonBlocking(Socket socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif
void SetOptions(Socket socket) {
    int yes = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_NOSIGPIPE
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}
int Write(Socket socket, const char* data, size_t length) {
#ifdef MSG_NOSIGNAL
    return static_cast<int>(send(socket, data, length, MSG_NOSIGNAL));
#else
    return static_cast<int>(send(socket, data, static_cast<int>(length), 0));
#endif
}
struct Peer {
    Socket socket = Invalid;
    FrameDecoder decoder;
    std::deque<std::string> outgoing;
    size_t offset = 0;
    size_t bytes = 0;
    bool connecting = false;
    bool closing = false;
    Clock::time_point opened = Clock::now();
    Clock::time_point lastRead = Clock::now();
    Clock::time_point lastWrite = Clock::now();
};
} // namespace
#endif

void Transport::Run(bool host, std::string address, uint16_t port) {
#ifdef SHIP_DIRECT_UNSUPPORTED
    (void)host; (void)address; (void)port;
    Push({ Event::Type::Error, 0, "Direct multiplayer currently supports desktop builds only." });
    running = false;
#else
#ifdef _WIN32
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        Push({ Event::Type::Error, 0, "Windows networking could not be initialized." });
        running = false;
        return;
    }
#endif
    Socket listener = Invalid;
    std::map<uint32_t, Peer> peers;
    uint32_t nextPeer = 2; // 1 is reserved for the actual in-process host.
    auto remove = [&](uint32_t id, const std::string& reason) {
        auto it = peers.find(id);
        if (it == peers.end()) return;
        Close(it->second.socket);
        peers.erase(it);
        if (!Push({ Event::Type::Disconnected, id, reason })) running = false;
        if (!host) running = false;
    };
    try {
        Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == Invalid) throw std::runtime_error("Cannot create a network socket.");
#ifndef _WIN32
        if (socket >= FD_SETSIZE) { Close(socket); throw std::runtime_error("Too many open sockets."); }
#endif
        if (!NonBlocking(socket)) { Close(socket); throw std::runtime_error("Cannot configure nonblocking networking."); }
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        endpoint.sin_addr.s_addr = INADDR_ANY;
        if (host) {
#ifdef _WIN32
            int yes = 1;
            setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
            int yes = 1;
            setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
            if (bind(socket, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) != 0 ||
                listen(socket, static_cast<int>(MaxPlayers)) != 0) {
                Close(socket);
                throw std::runtime_error("Cannot host: port is already in use or access was denied.");
            }
            listener = socket;
            Push({ Event::Type::Listening, 1, "Listening on TCP port " + std::to_string(port) });
        } else {
            // Parse decimal ourselves so leading zeroes never acquire octal semantics.
            uint32_t ip = 0, component = 0;
            for (char c : address + '.') {
                if (c == '.') { ip = (ip << 8) | component; component = 0; }
                else component = component * 10 + static_cast<unsigned>(c - '0');
            }
            endpoint.sin_addr.s_addr = htonl(ip);
            SetOptions(socket);
            int result = connect(socket, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint));
            if (result != 0 && !WouldBlock()) {
                Close(socket);
                throw std::runtime_error("Connection failed. Check the host IP, port and firewall.");
            }
            Peer peer;
            peer.socket = socket;
            peer.connecting = result != 0;
            peers.emplace(1, std::move(peer));
            if (result == 0) Push({ Event::Type::Connected, 1, "Connected" });
        }
        while (running) {
            std::deque<Command> pending;
            {
                std::lock_guard<std::mutex> lock(mutex);
                pending.swap(commands);
                commandBytes = 0;
            }
            for (auto& command : pending) {
                auto it = peers.find(command.peer);
                if (it == peers.end()) continue;
                if (command.drop) { it->second.closing = true; continue; }
                auto& peer = it->second;
                if (peer.closing) continue;
                if (peer.bytes + command.frame.size() > MaxQueueBytes || peer.outgoing.size() >= 4096) {
                    remove(command.peer, "Slow peer exceeded the outgoing queue limit.");
                    continue;
                }
                if (peer.outgoing.empty()) peer.lastWrite = Clock::now();
                peer.bytes += command.frame.size();
                peer.outgoing.push_back(std::move(command.frame));
            }
            fd_set reads, writes, errors;
            FD_ZERO(&reads); FD_ZERO(&writes); FD_ZERO(&errors);
            Socket highest = 0;
            if (listener != Invalid) { FD_SET(listener, &reads); highest = listener; }
            for (const auto& [id, peer] : peers) {
                (void)id;
                FD_SET(peer.socket, &errors);
                FD_SET(peer.socket, &reads);
                if (peer.connecting || !peer.outgoing.empty()) FD_SET(peer.socket, &writes);
                highest = std::max(highest, peer.socket);
            }
            timeval timeout{ 0, 10000 }; // No busy spin; Stop is bounded by this poll.
            int selected = select(static_cast<int>(highest + 1), &reads, &writes, &errors, &timeout);
            if (selected < 0) {
                if (WouldBlock()) continue;
                throw std::runtime_error("Socket polling failed.");
            }
            if (listener != Invalid && FD_ISSET(listener, &reads)) {
                Socket accepted = accept(listener, nullptr, nullptr);
                if (accepted != Invalid) {
                    bool admissible = peers.size() < MaxPlayers - 1 && NonBlocking(accepted);
#ifndef _WIN32
                    admissible = admissible && accepted < FD_SETSIZE;
#endif
                    if (!admissible) Close(accepted);
                    else {
                        SetOptions(accepted);
                        Peer peer; peer.socket = accepted;
                        uint32_t id = nextPeer++;
                        if (nextPeer == 0) nextPeer = 2;
                        peers.emplace(id, std::move(peer));
                        if (!Push({ Event::Type::Accepted, id, "Incoming connection" })) {
                            remove(id, "Main-thread receive queue is full.");
                        }
                    }
                }
            }
            std::vector<std::pair<uint32_t, std::string>> dead;
            for (auto& [id, peer] : peers) {
                auto now = Clock::now();
                if (peer.closing && peer.outgoing.empty()) { dead.emplace_back(id, "Connection closed."); continue; }
                if (FD_ISSET(peer.socket, &errors)) {
                    dead.emplace_back(id, "Connection failed."); continue;
                }
                if (peer.connecting) {
                    if (now - peer.opened > std::chrono::seconds(5)) {
                        dead.emplace_back(id, "Connection timed out. Check the host IP, port and firewall."); continue;
                    }
                    if (!FD_ISSET(peer.socket, &writes) && !FD_ISSET(peer.socket, &reads)) continue;
                    int error = 0;
#ifdef _WIN32
                    int length = sizeof(error);
#else
                    socklen_t length = sizeof(error);
#endif
                    if (getsockopt(peer.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error) {
                        dead.emplace_back(id, "Host refused the connection, or could not be reached."); continue;
                    }
                    peer.connecting = false;
                    if (!Push({ Event::Type::Connected, id, "Connected" })) {
                        dead.emplace_back(id, "Main-thread receive queue is full."); continue;
                    }
                }
                if (now - peer.lastRead > std::chrono::seconds(30)) {
                    dead.emplace_back(id, "Connection heartbeat timed out."); continue;
                }
                if (!peer.outgoing.empty() && now - peer.lastWrite > std::chrono::seconds(5)) {
                    dead.emplace_back(id, "Peer stopped receiving data."); continue;
                }
                if (FD_ISSET(peer.socket, &reads)) {
                    std::array<char, 16384> buffer;
                    int count = static_cast<int>(recv(peer.socket, buffer.data(), static_cast<int>(buffer.size()), 0));
                    if (count <= 0) {
                        if (count == 0 || !WouldBlock()) dead.emplace_back(id, "Peer disconnected.");
                        continue;
                    }
                    peer.lastRead = now;
                    std::vector<std::string> frames;
                    if (!peer.decoder.Feed(buffer.data(), static_cast<size_t>(count), frames)) {
                        dead.emplace_back(id, "Invalid or oversized network frame."); continue;
                    }
                    for (auto& frame : frames) {
                        if (!Push({ Event::Type::Message, id, std::move(frame) })) {
                            dead.emplace_back(id, "Main-thread receive queue is full."); break;
                        }
                    }
                }
                if (!peer.outgoing.empty() && FD_ISSET(peer.socket, &writes)) {
                    auto& frame = peer.outgoing.front();
                    size_t length = std::min<size_t>(frame.size() - peer.offset, 65536);
                    int count = Write(peer.socket, frame.data() + peer.offset, length);
                    if (count < 0 && !WouldBlock()) { dead.emplace_back(id, "Network send failed."); continue; }
                    if (count > 0) {
                        peer.lastWrite = now;
                        peer.offset += static_cast<size_t>(count);
                        peer.bytes -= static_cast<size_t>(count);
                        if (peer.offset == frame.size()) { peer.outgoing.pop_front(); peer.offset = 0; }
                    }
                }
            }
            for (const auto& [id, reason] : dead) remove(id, reason);
        }
    } catch (const std::exception& error) {
        Push({ Event::Type::Error, 0, error.what() });
    }
    for (auto& [id, peer] : peers) { (void)id; Close(peer.socket); }
    Close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    running = false;
#endif
}
} // namespace Shipwright::Direct
