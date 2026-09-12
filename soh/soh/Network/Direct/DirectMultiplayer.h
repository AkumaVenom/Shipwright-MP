#pragma once
#include "Transport.h"
#include "soh/cvar_prefixes.h"
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>

namespace Shipwright::Direct {
enum class LaunchMode { Offline, Host, Join };

// Engine integration runs exclusively on the main game thread. The transport
// never reads gSaveContext, CVar state, actors, or hooks.
class Session final {
  public:
    static Session& Get();
    void Initialize();
    bool Start(LaunchMode mode, const std::string& ip, int port, const std::string& name, bool shareProgress);
    void Stop(const std::string& reason = "Offline");
    void Tick();
    bool Active() const { return active; }
    bool Hosting() const { return active && mode == LaunchMode::Host; }
    bool WaitingForSave() const { return waiting; }
    bool SharingProgress() const { return shareProgress; }
    bool Ready() const { return ready; }
    const std::string& Status() const { return status; }
    const std::string& BackupPath() const { return backupPath; }
    const std::string& PlayerName() const { return name; }
    void Send(nlohmann::json payload);
    void ApplySharedState(const nlohmann::json& payload);
    void PrepareForSaveChange();
    void Shutdown();
  private:
    using Clock = std::chrono::steady_clock;
    struct PeerState {
        bool authenticated = false;
        bool closing = false;
        nlohmann::json state;
        Clock::time_point accepted = Clock::now();
        Clock::time_point rateWindow = Clock::now();
        size_t messages = 0;
    };
    Transport transport;
    std::map<uint32_t, PeerState> peers;
    std::map<std::pair<uint32_t, uint32_t>, Clock::time_point> teleports;
    bool initialized = false;
    bool active = false;
    bool waiting = false;
    bool ready = false;
    bool welcomed = false;
    bool receivedRoster = false;
    bool receivedSnapshot = false;
    bool shareProgress = false;
    bool applying = false;
    bool readOnlyFailure = false;
    LaunchMode mode = LaunchMode::Offline;
    std::string ip;
    uint16_t port = DefaultPort;
    std::string name;
    std::string status = "Offline";
    std::string backupPath;
    std::string compatibility;
    int file = -1;
    Clock::time_point heartbeat{};
    Clock::time_point handshakeStarted{};
    void Begin();
    bool BackupSelectedSave();
    nlohmann::json Identity() const;
    nlohmann::json LocalState() const;
    void Transmit(uint32_t peer, const nlohmann::json& packet);
    void Handle(uint32_t peer, nlohmann::json packet);
    void Relay(uint32_t sender, nlohmann::json packet);
    void Roster();
    void BroadcastSnapshot();
    void Reject(uint32_t peer, const std::string& reason);
    bool ValidateClientState(const nlohmann::json& state) const;
    bool ValidatePlayerUpdate(const nlohmann::json& packet) const;
    bool ValidateSharedEvent(const nlohmann::json& packet) const;
    bool ValidateSnapshot(const nlohmann::json& packet) const;
    nlohmann::json MergeSaveOffer(const nlohmann::json& offer) const;
    void Deliver(nlohmann::json packet);
};
} // namespace Shipwright::Direct

#define CVAR_DIRECT(name) CVAR_REMOTE("Direct." name)
