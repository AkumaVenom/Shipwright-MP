#include "DirectMultiplayer.h"
#include "ProgressRules.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/SaveManager.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/randomizer_check_tracker.h"
#include "soh/Notification/Notification.h"
#include <ship/Context.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

extern "C" {
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace Shipwright::Direct {
namespace {
using Json = nlohmann::json;
constexpr const char* BuildId = "Shipwright-MP-1.0.0-direct-v1";
constexpr const char* Team = "direct-session";

bool Number(const Json& p, const char* key, int64_t low, int64_t high) {
    if (!p.contains(key) || !p.at(key).is_number_integer()) return false;
    if (p.at(key).is_number_unsigned() && p.at(key).get<uint64_t>() > static_cast<uint64_t>(high)) return false;
    auto n = p.at(key).get<int64_t>();
    return n >= low && n <= high;
}
bool Vector(const Json& p, bool integral) {
    if (!p.is_object()) return false;
    for (const char* axis : { "x", "y", "z" }) {
        if (!p.contains(axis) || !p.at(axis).is_number()) return false;
        double n = p.at(axis).get<double>();
        if (!std::isfinite(n) || n < (integral ? -32768.0 : -1000000.0) ||
            n > (integral ? 32767.0 : 1000000.0)) return false;
        if (integral && !p.at(axis).is_number_integer()) return false;
    }
    return true;
}
bool Position(const Json& p) {
    return p.is_object() && p.contains("pos") && p.contains("rot") && Vector(p.at("pos"), false) &&
           Vector(p.at("rot"), true);
}
bool ValidName(const std::string& name) {
    if (name.empty() || name.size() > 32 || name.find_first_not_of(' ') == std::string::npos) return false;
    for (unsigned char c : name) if (c < 32 || c == 127) return false;
    return true;
}
bool SameShape(const Json& actual, const Json& reference, unsigned depth = 0) {
    if (depth > 16) return false;
    if (reference.is_object()) {
        if (!actual.is_object() || actual.size() != reference.size()) return false;
        for (auto it = reference.begin(); it != reference.end(); ++it)
            if (!actual.contains(it.key()) || !SameShape(actual.at(it.key()), it.value(), depth + 1)) return false;
        return true;
    }
    if (reference.is_array()) {
        if (!actual.is_array() || actual.size() != reference.size()) return false;
        for (size_t i = 0; i < reference.size(); ++i)
            if (!SameShape(actual[i], reference[i], depth + 1)) return false;
        return true;
    }
    if (reference.is_number_integer()) {
        if (!actual.is_number_integer()) return false;
        // More precise field ranges are checked before SaveContext conversion.
        return true; // Save timestamps are 64-bit; individual consumed fields are checked below.
    }
    return actual.type() == reference.type();
}
bool ArrayRange(const Json& a, int64_t low, int64_t high) {
    if (!a.is_array()) return false;
    for (const auto& item : a) {
        Json wrapper{ { "v", item } };
        if (!Number(wrapper, "v", low, high)) return false;
    }
    return true;
}
const std::set<std::string> SharedTypes{
    "GIVE_ITEM", "SET_FLAG", "UNSET_FLAG", "SET_CHECK_STATUS", "UPDATE_DUNGEON_ITEMS",
    "UPDATE_BEANS_COUNT", "ENTRANCE_DISCOVERED", "GAME_COMPLETE"
};
} // namespace

Session& Session::Get() {
    static Session instance;
    return instance;
}
void Session::Initialize() {
    if (initialized) return;
    initialized = true;
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() { Get().Tick(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) {
        // The ordinary save/autosave pipeline remains the only save writer.
        if (Get().Active() && !Get().WaitingForSave()) Get().Stop("Session ended: returned to file select.");
    });
}
bool Session::Start(LaunchMode requestedMode, const std::string& address, int requestedPort,
                    const std::string& playerName, bool requestedSharing) {
    if (active) { status = "Leave the current session before starting another."; return false; }
    if (requestedMode == LaunchMode::Offline) { status = "Offline: use the normal file-select screen."; return true; }
    if ((requestedMode != LaunchMode::Host && requestedMode != LaunchMode::Join) || requestedPort < 1024 || requestedPort > 65535 || !ValidName(playerName) ||
        (requestedMode == LaunchMode::Join && !IsIPv4(address))) {
        status = "Use a player name (1-32 bytes), TCP port 1024-65535, and a numeric host IPv4 address.";
        return false;
    }
    auto* anchor = Anchor::Instance;
    if (anchor == nullptr) { status = "Networking is not initialized yet."; return false; }
    if (anchor->isEnabled) { status = "Disable the existing Anchor connection first."; return false; }
    mode = requestedMode;
    ip = address;
    port = static_cast<uint16_t>(requestedPort);
    name = playerName;
    shareProgress = requestedSharing;
    file = -1;
    backupPath.clear();
    compatibility.clear();
    ready = welcomed = receivedRoster = receivedSnapshot = false;
    active = waiting = true;
    readOnlyFailure = false;
    status = "Ready to launch: load your own save slot normally. No remote save will select a slot for you.";
    if (anchor->IsSaveLoaded()) {
        try { Begin(); } catch (const std::exception& e) { Stop(std::string("Could not launch multiplayer: ") + e.what()); }
    }
    return active;
}
bool Session::BackupSelectedSave() {
    try {
        SaveManager::Instance->ThreadPoolWait();
        const auto root = std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("Save"));
        const auto source = root / ("file" + std::to_string(file + 1) + ".sav");
        if (!std::filesystem::is_regular_file(source)) {
            status = "Save backup failed: save your selected file locally before starting multiplayer.";
            return false;
        }
        auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto directory = root / "MultiplayerBackups" / (std::to_string(stamp) + "-slot" + std::to_string(file + 1));
        std::filesystem::create_directories(directory);
        const auto destination = directory / source.filename();
        if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none)) return false;
        // Retain unsaved shared progression as a diagnostic snapshot too. This
        // file is NOT passed to SaveManager and is not a replacement .sav file.
        std::ofstream entry(directory / "session-entry-progress.json", std::ios::binary);
        entry << Anchor::Instance->BuildTeamState().dump(2);
        entry.close();
        if (!entry) { status = "Cannot write the session-entry backup. Connection cancelled."; return false; }
        backupPath = destination.string();
        return true;
    } catch (const std::exception& e) {
        status = std::string("Save backup failed; connection cancelled: ") + e.what();
        return false;
    }
}
Json Session::Identity() const {
    Json result{ { "build", BuildId }, { "source", Anchor::clientVersion },
                 { "quest", static_cast<int>(gSaveContext.ship.quest.id) },
                 { "seed", IS_RANDO ? Rando::Context::GetInstance()->GetSeed() : 0 } };
    if (IS_RANDO) {
        // Matching seed numbers alone are not sufficient if settings differ.
        Json settings = Json::array();
        auto context = Rando::Context::GetInstance();
        for (int i = 0; i < RSK_MAX; ++i)
            settings.push_back(context->GetOption(static_cast<RandomizerSettingKey>(i)).Get());
        result["settings"] = std::move(settings);
    }
    return result;
}
Json Session::LocalState() const {
    Json state = Anchor::Instance->PrepClientState();
    state["name"] = name;
    state["teamId"] = Team;
    state["online"] = true;
    return state;
}
void Session::Begin() {
    auto* anchor = Anchor::Instance;
    if (!anchor->IsSaveLoaded()) return;
    if (gSaveContext.ship.quest.id == QUEST_BOSSRUSH) {
        active = waiting = false;
        status = "Boss Rush does not support direct multiplayer. Load a normal, MQ or matching randomizer save.";
        return;
    }
    file = gSaveContext.fileNum;
    if (!BackupSelectedSave()) { active = waiting = false; return; }
    compatibility = Identity().dump();
    waiting = false;
    anchor->ownClientId = Hosting() ? 1 : 0;
    anchor->roomState = {};
    anchor->clients.clear();
    anchor->isEnabled = true;
    anchor->isConnected = false;
    heartbeat = handshakeStarted = Clock::now();
    bool started = Hosting() ? transport.Host(port) : transport.Join(ip, port);
    if (!started) { Stop("Could not start the network transport."); return; }
    status = Hosting() ? "Opening host listener..." : "Connecting to " + ip + ":" + std::to_string(port) + "...";
}
void Session::Stop(const std::string& reason) {
    bool wasActive = active;
    active = waiting = ready = welcomed = receivedRoster = receivedSnapshot = false;
    transport.Stop();
    peers.clear();
    teleports.clear();
    if (wasActive && Anchor::Instance != nullptr) {
        auto* anchor = Anchor::Instance;
        // OnSaveFile executes on the save worker; let it finish before changing hook registrations.
        if (SaveManager::Instance != nullptr) SaveManager::Instance->ThreadPoolWait();
        anchor->isEnabled = false;
        anchor->isConnected = false;
        anchor->RegisterHooks();
        anchor->clients.clear();
        anchor->RefreshClientActors();
        anchor->roomState = {};
        anchor->ownClientId = 0;
        anchor->justLoadedSave = false;
        anchor->saveSnapshotRequested = false;
        anchor->shouldRefreshActors = false;
        {
            std::lock_guard<std::mutex> lock(anchor->incomingPacketQueueMutex);
            anchor->incomingPacketQueue = {};
        }
        {
            std::lock_guard<std::mutex> lock(anchor->outgoingPacketQueueMutex);
            anchor->outgoingPacketQueue = {};
        }
    }
    file = -1;
    status = reason;
}
void Session::PrepareForSaveChange() {
    // Called before SaveManager takes its save mutex. Never wait for/save a file
    // from an OnLoadFile callback (that callback already holds the mutex).
    if (active && !waiting) Stop("Session ended before changing save files. Your other slots were not synchronized.");
}
void Session::Shutdown() { Stop("Offline"); }
void Session::Transmit(uint32_t peer, const Json& packet) {
    if (!transport.Send(peer, packet.dump())) {
        if (Hosting()) transport.Drop(peer);
        else readOnlyFailure = true;
    }
}
void Session::Reject(uint32_t peer, const std::string& reason) {
    Transmit(peer, Json{ { "type", "DIRECT_REJECT" }, { "reason", reason } });
    transport.Drop(peer);
    if (peers.contains(peer)) { peers.at(peer).authenticated = false; peers.at(peer).closing = true; }
}
void Session::Deliver(Json packet) {
    auto* anchor = Anchor::Instance;
    anchor->OnIncomingJson(std::move(packet));
    anchor->ProcessIncomingPacketQueue();
}
void Session::Roster() {
    if (!Hosting()) return;
    Json states = Json::array();
    auto own = LocalState(); own["clientId"] = 1;
    states.push_back(own);
    for (const auto& [id, peer] : peers) {
        if (!peer.authenticated) continue;
        auto state = peer.state; state["clientId"] = id;
        states.push_back(std::move(state));
    }
    for (const auto& target : states) {
        uint32_t id = target.at("clientId").get<uint32_t>();
        auto tailored = states;
        for (auto& state : tailored) state["self"] = state.at("clientId").get<uint32_t>() == id;
        Json packet{ { "type", Anchor::ALL_CLIENT_STATE }, { "state", std::move(tailored) } };
        if (id == 1) Deliver(std::move(packet));
        else Transmit(id, packet);
    }
}
void Session::BroadcastSnapshot() {
    if (!Hosting() || !ready || !shareProgress || !Anchor::Instance->IsSaveLoaded()) return;
    auto snapshot = Anchor::Instance->BuildTeamState();
    snapshot["clientId"] = 1;
    snapshot["queue"] = Json::array();
    for (const auto& [id, peer] : peers) if (peer.authenticated) Transmit(id, snapshot);
}
void Session::Tick() {
    if (!active) return;
    auto* anchor = Anchor::Instance;
    if (waiting) {
        if (anchor->IsSaveLoaded()) {
            try { Begin(); } catch (const std::exception& e) { Stop(std::string("Could not launch multiplayer: ") + e.what()); }
        }
        return;
    }
    if (gSaveContext.fileNum != file) {
        Stop("Session ended because the selected save changed."); return;
    }
    // Scene teardown can briefly clear gPlayState. Do not treat an ordinary
    // entrance/age transition as leaving the session; retain bounded messages
    // until the next valid play state. OnExitGame handles actual file-select exit.
    if (!anchor->IsSaveLoaded()) return;
    auto now = Clock::now();
    for (auto& event : transport.Poll()) {
        if (!active) break;
        try {
            switch (event.type) {
                case Event::Type::Listening:
                    anchor->roomState = { 1, 0, 2, 2, static_cast<u8>(shareProgress) };
                    anchor->isConnected = ready = true;
                    anchor->RegisterHooks();
                    Roster();
                    status = "Hosting on TCP port " + std::to_string(port) + " (up to 8 players).";
                    break;
                case Event::Type::Accepted:
                    peers.emplace(event.peer, PeerState{});
                    break;
                case Event::Type::Connected: {
                    auto hello = Json{ { "type", "DIRECT_HELLO" }, { "protocol", ProtocolVersion },
                                       { "identity", Identity() }, { "state", LocalState() },
                                       { "allowSharedProgress", shareProgress } };
                    if (shareProgress) hello["save"] = anchor->BuildTeamState();
                    Transmit(1, hello);
                    status = "Connected; checking build, save compatibility and sharing permissions...";
                    handshakeStarted = now;
                    break;
                }
                case Event::Type::Message: {
                    // Bound nesting before JSON parsing; no malicious nested payload
                    // should exhaust the parser stack even within the byte cap.
                    unsigned depth = 0; bool quoted = false, escaped = false;
                    for (char c : event.text) {
                        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
                        else if (c == '"') quoted = true;
                        else if (c == '[' || c == '{') { if (++depth > 24) throw std::runtime_error("Packet is nested too deeply"); }
                        else if (c == ']' || c == '}') { if (depth == 0) throw std::runtime_error("Invalid packet nesting"); --depth; }
                    }
                    auto packet = Json::parse(event.text);
                    if (!packet.is_object() || !packet.contains("type") || !packet.at("type").is_string())
                        throw std::runtime_error("Malformed packet envelope");
                    Handle(event.peer, std::move(packet));
                    break;
                }
                case Event::Type::Disconnected:
                    if (!Hosting()) Stop(status.rfind("Rejected:", 0) == 0 ? status : event.text + " Local progress remains in your own game.");
                    else { peers.erase(event.peer); Roster(); }
                    break;
                case Event::Type::Error:
                    Stop(event.text);
                    break;
            }
        } catch (const std::exception& e) {
            if (Hosting()) Reject(event.peer, std::string("Invalid session packet: ") + e.what());
            else Stop(std::string("Session packet rejected: ") + e.what());
        }
    }
    if (!active) return;
    if (!transport.Running()) {
        // An event-queue overflow can stop the worker before it can enqueue a
        // final error event. Never leave the UI claiming a dead listener is live.
        Stop("Network transport stopped or exceeded its queue limits. Local progress was retained.");
        return;
    }
    if (readOnlyFailure) { Stop("Connection stopped: outgoing queue overflow. Local progress was retained."); return; }
    if (!Hosting() && !ready && now - handshakeStarted > std::chrono::seconds(10)) {
        Stop("Session handshake timed out. Use the same multiplayer build on both PCs."); return;
    }
    if (Hosting()) {
        for (const auto& [id, peer] : peers)
            if (!peer.authenticated && now - peer.accepted > std::chrono::seconds(8)) transport.Drop(id);
    }
    if (now - heartbeat > std::chrono::seconds(3)) {
        heartbeat = now;
        Json ping{ { "type", "DIRECT_PING" } };
        if (Hosting()) {
            for (const auto& [id, peer] : peers) if (peer.authenticated) Transmit(id, ping);
        } else Transmit(1, ping);
    }
    for (auto it = teleports.begin(); it != teleports.end();) {
        if (now - it->second > std::chrono::seconds(10)) it = teleports.erase(it); else ++it;
    }
}

void Session::Handle(uint32_t peerId, Json packet) {
    const std::string type = packet.at("type").get<std::string>();
    auto* anchor = Anchor::Instance;
    if (!Hosting()) {
        if (type == "DIRECT_REJECT") {
            Stop("Rejected: " + packet.value("reason", std::string("incompatible session")).substr(0, 256));
            return;
        }
        if (type == "DIRECT_PING") { Transmit(1, Json{ { "type", "DIRECT_PONG" } }); return; }
        if (type == "DIRECT_PONG") return;
        if (type == "DIRECT_WELCOME") {
            if (welcomed || packet.at("protocol").get<uint32_t>() != ProtocolVersion ||
                packet.at("identity").dump() != compatibility || !Number(packet, "clientId", 2, UINT32_MAX))
                throw std::runtime_error("Host build or save identity mismatch");
            bool shared = packet.at("sharedProgress").get<bool>();
            if (shared && !shareProgress) throw std::runtime_error("Host requires shared-save consent");
            shareProgress = shared;
            anchor->ownClientId = packet.at("clientId").get<uint32_t>();
            anchor->roomState = { 1, 0, 2, 2, static_cast<u8>(shareProgress) };
            welcomed = true;
            status = "Accepted; synchronizing the player roster and agreed save state...";
            return;
        }
        if (type == "DIRECT_READY") {
            if (ready || !welcomed || !receivedRoster || (shareProgress && !receivedSnapshot))
                throw std::runtime_error("Host did not finish initial synchronization");
            anchor->isConnected = ready = true;
            anchor->RegisterHooks();
            status = "Joined " + ip + ":" + std::to_string(port) +
                     (shareProgress ? " - shared adventure, own local save slot." : " - personal save progress retained.");
            return;
        }
        if (!welcomed || (!ready && type != Anchor::ALL_CLIENT_STATE && type != Anchor::UPDATE_TEAM_STATE))
            throw std::runtime_error("Gameplay arrived before initial synchronization");
        if (type == Anchor::ALL_CLIENT_STATE) {
            const auto& states = packet.at("state");
            if (!states.is_array() || states.empty() || states.size() > MaxPlayers)
                throw std::runtime_error("Invalid player roster");
            std::set<uint32_t> ids; unsigned selfCount = 0;
            for (const auto& state : states) {
                if (!ValidateClientState(state) || !Number(state, "clientId", 1, UINT32_MAX) ||
                    !state.contains("self") || !state.at("self").is_boolean())
                    throw std::runtime_error("Invalid roster entry");
                auto id = state.at("clientId").get<uint32_t>();
                if (!ids.insert(id).second || state.at("self").get<bool>() != (id == anchor->ownClientId))
                    throw std::runtime_error("Invalid roster identity");
                if (id == anchor->ownClientId) ++selfCount;
            }
            if (selfCount != 1 || !ids.contains(1)) throw std::runtime_error("Roster omitted the host or own player");
            receivedRoster = true;
        } else {
            if (!Number(packet, "clientId", 1, UINT32_MAX)) throw std::runtime_error("Missing sender identity");
            if (!anchor->clients.contains(packet.at("clientId").get<uint32_t>()))
                throw std::runtime_error("Unknown sender identity");
        }
        if (type == Anchor::UPDATE_CLIENT_STATE && !ValidateClientState(packet.at("state")))
            throw std::runtime_error("Invalid player state");
        if (type == Anchor::PLAYER_SFX && !Number(packet, "sfxId", 0, 65535))
            throw std::runtime_error("Invalid sound identifier");
        if (type == Anchor::OCARINA_SFX &&
            ((!Number(packet, "note", 0, 4) && !Number(packet, "note", 255, 255)) ||
             !Number(packet, "bend", -128, 127) || !packet.at("modulator").is_number() ||
             !std::isfinite(packet.at("modulator").get<float>()) || std::abs(packet.at("modulator").get<float>()) > 16))
            throw std::runtime_error("Invalid ocarina sound");
        if (type == Anchor::TELEPORT_TO) {
            auto source = packet.at("clientId").get<uint32_t>();
            auto request = teleports.find({ anchor->ownClientId, source });
            if (request == teleports.end() || Clock::now() - request->second > std::chrono::seconds(10) ||
                !Number(packet, "entranceIndex", 0, ENTR_MAX - 1) || !Number(packet, "roomIndex", 0, 63) ||
                !packet.contains("posRot") || !Position(packet.at("posRot")))
                throw std::runtime_error("Unsolicited or invalid teleport");
            teleports.erase(request);
        }
        if (type == Anchor::PLAYER_UPDATE && !ValidatePlayerUpdate(packet)) throw std::runtime_error("Invalid player pose");
        if (SharedTypes.contains(type) && (!shareProgress || !ValidateSharedEvent(packet))) return;
        if (type == Anchor::UPDATE_TEAM_STATE) {
            if (packet.at("clientId").get<uint32_t>() != 1 || !shareProgress || !ValidateSnapshot(packet))
                throw std::runtime_error("Invalid shared save snapshot");
            receivedSnapshot = true;
        }
        const std::set<std::string> allowed{ Anchor::ALL_CLIENT_STATE, Anchor::UPDATE_CLIENT_STATE, Anchor::PLAYER_UPDATE,
            Anchor::PLAYER_SFX, Anchor::OCARINA_SFX, Anchor::REQUEST_TELEPORT, Anchor::TELEPORT_TO, Anchor::UPDATE_TEAM_STATE };
        if (!allowed.contains(type) && !SharedTypes.contains(type)) throw std::runtime_error("Unexpected host packet");
        Deliver(std::move(packet));
        return;
    }
    if (!peers.contains(peerId)) return;
    auto& peer = peers.at(peerId);
    if (peer.closing) return;
    auto now = Clock::now();
    if (now - peer.rateWindow >= std::chrono::seconds(1)) { peer.rateWindow = now; peer.messages = 0; }
    if (++peer.messages > 800) { Reject(peerId, "Packet rate limit exceeded."); return; }
    if (type == "DIRECT_HELLO") {
        if (peer.authenticated) { Reject(peerId, "Duplicate handshake."); return; }
        if (packet.at("protocol").get<uint32_t>() != ProtocolVersion || packet.at("identity").dump() != compatibility) {
            Reject(peerId, "Use the identical multiplayer build and quest type; randomizer seeds and settings must match.");
            return;
        }
        auto state = packet.at("state");
        if (!ValidateClientState(state) || !state.at("isSaveLoaded").get<bool>() ||
            !Number(state, "sceneNum", 0, SCENE_ID_MAX - 1)) {
            Reject(peerId, "Load your own valid save and choose a player name before joining."); return;
        }
        state["clientId"] = peerId;
        state["seed"] = Identity().at("seed");
        state["teamId"] = Team;
        state["clientVersion"] = Anchor::clientVersion;
        state["online"] = true;
        state["self"] = false;
        (void)state.get<AnchorClient>(); // Validate the entire metadata before touching shared progress.
        if (shareProgress) {
            if (!packet.value("allowSharedProgress", false)) {
                Reject(peerId, "This host shares adventure progress. Enable 'Allow shared progress' and join again; a backup is made first.");
                return;
            }
            if (!packet.contains("save") || !ValidateSnapshot(packet.at("save"))) {
                Reject(peerId, "The selected save does not match the session's save schema."); return;
            }
            // Only the host merges offers, on its own game thread, before this
            // peer is admitted to gameplay or receives anyone else's state.
            auto merged = MergeSaveOffer(packet.at("save"));
            ApplySharedState(merged);
        }
        peer.state = std::move(state);
        peer.authenticated = true;
        Transmit(peerId, Json{ { "type", "DIRECT_WELCOME" }, { "protocol", ProtocolVersion },
                              { "identity", Identity() }, { "clientId", peerId }, { "sharedProgress", shareProgress } });
        Roster();
        BroadcastSnapshot();
        Transmit(peerId, Json{ { "type", "DIRECT_READY" } });
        return;
    }
    if (!peer.authenticated) { Reject(peerId, "Handshake required."); return; }
    if (type == "DIRECT_PING") { Transmit(peerId, Json{ { "type", "DIRECT_PONG" } }); return; }
    if (type == "DIRECT_PONG") return;
    Relay(peerId, std::move(packet));
}

void Session::Send(Json packet) {
    if (!active || !ready || applying) return;
    if (mode == LaunchMode::Host) Relay(1, std::move(packet));
    else {
        const auto type = packet.value("type", std::string{});
        if (type == Anchor::UPDATE_TEAM_STATE || type == Anchor::UPDATE_ROOM_STATE || type == Anchor::HANDSHAKE) return;
        if (type == Anchor::UPDATE_CLIENT_STATE) packet["state"] = LocalState();
        if (type == Anchor::REQUEST_TELEPORT && Number(packet, "targetClientId", 1, UINT32_MAX))
            teleports[{ Anchor::Instance->ownClientId, packet.at("targetClientId").get<uint32_t>() }] = Clock::now();
        // Clients cannot supply their own authoritative sender identity.
        packet.erase("clientId");
        Transmit(1, packet);
    }
}
void Session::Relay(uint32_t sender, Json packet) {
    const auto type = packet.value("type", std::string{});
    packet["clientId"] = sender;
    packet.erase("addToQueue");
    packet.erase("targetTeamId");
    auto to = [&](uint32_t target, const Json& message) {
        if (target == 1) Deliver(message);
        else if (peers.contains(target) && peers.at(target).authenticated) Transmit(target, message);
    };
    if (type == Anchor::UPDATE_CLIENT_STATE) {
        auto state = sender == 1 ? LocalState() : packet.at("state");
        if (!ValidateClientState(state)) return;
        if (sender != 1) {
            // Name, identity, team and seed are fixed by admission, not by a
            // later client-supplied state packet.
            auto& known = peers.at(sender).state;
            for (const char* key : { "sceneNum", "curRoomNum", "entranceIndex", "isSaveLoaded", "isGameComplete" })
                if (state.contains(key)) known[key] = state.at(key);
            state = known;
        }
        packet["state"] = state;
        if (sender != 1) to(1, packet);
        for (const auto& [id, peer] : peers) if (peer.authenticated && id != sender) to(id, packet);
        return;
    }
    if (type == Anchor::REQUEST_TEAM_STATE) { if (shareProgress) BroadcastSnapshot(); return; }
    if (type == Anchor::UPDATE_TEAM_STATE) { if (sender == 1) BroadcastSnapshot(); return; }
    if (SharedTypes.contains(type)) {
        if (!shareProgress || !ValidateSharedEvent(packet)) return;
        // Apply to host first. Joining peers therefore receive a snapshot that
        // includes all already accepted mutations, not a stale server cache.
        if (sender != 1) to(1, packet);
        for (const auto& [id, peer] : peers) if (peer.authenticated && id != sender) to(id, packet);
        return;
    }
    if (!Number(packet, "targetClientId", 1, UINT32_MAX)) return;
    uint32_t target = packet.at("targetClientId").get<uint32_t>();
    if (target == sender || (target != 1 && (!peers.contains(target) || !peers.at(target).authenticated))) return;
    auto sourceState = sender == 1 ? LocalState() : peers.at(sender).state;
    auto targetState = target == 1 ? LocalState() : peers.at(target).state;
    if (type == Anchor::PLAYER_UPDATE) {
        if (!ValidatePlayerUpdate(packet) || sourceState.at("sceneNum") != targetState.at("sceneNum") ||
            packet.at("sceneNum") != sourceState.at("sceneNum")) return;
    } else if (type == Anchor::PLAYER_SFX) {
        if (!Number(packet, "sfxId", 0, 65535) || sourceState.at("sceneNum") != targetState.at("sceneNum")) return;
    } else if (type == Anchor::OCARINA_SFX) {
        if ((!Number(packet, "note", 0, 4) && !Number(packet, "note", 255, 255)) ||
            !Number(packet, "bend", -128, 127) || !packet.contains("modulator") || !packet.at("modulator").is_number() ||
            !std::isfinite(packet.at("modulator").get<float>()) || std::abs(packet.at("modulator").get<float>()) > 16.0f ||
            sourceState.at("sceneNum") != targetState.at("sceneNum")) return;
    } else if (type == Anchor::REQUEST_TELEPORT) {
        teleports[{ sender, target }] = Clock::now();
    } else if (type == Anchor::TELEPORT_TO) {
        auto request = teleports.find({ target, sender });
        if (request == teleports.end() || Clock::now() - request->second > std::chrono::seconds(10) ||
            !Number(packet, "entranceIndex", 0, ENTR_MAX - 1) || !Number(packet, "roomIndex", 0, 63) ||
            !packet.contains("posRot") || !Position(packet.at("posRot"))) return;
        teleports.erase(request);
    } else return; // Includes forged roster/admin packets and unsolicited PvP.
    to(target, packet);
}

bool Session::ValidateClientState(const Json& s) const {
    if (!s.is_object() || !s.contains("name") || !s.at("name").is_string() ||
        !ValidName(s.at("name").get<std::string>()) || !Number(s, "sceneNum", 0, SCENE_ID_MAX) ||
        !Number(s, "curRoomNum", -1, 63) || !Number(s, "entranceIndex", 0, 65535) ||
        !Number(s, "seed", 0, UINT32_MAX)) return false;
    for (const char* field : { "online", "isSaveLoaded", "isGameComplete" })
        if (!s.contains(field) || !s.at(field).is_boolean()) return false;
    if (!s.contains("color") || !s.at("color").is_object()) return false;
    for (const char* component : { "r", "g", "b" })
        if (!Number(s.at("color"), component, 0, 255)) return false;
    return s.value("clientVersion", std::string{}) == Anchor::clientVersion && s.value("teamId", std::string{}) == Team;
}
bool Session::ValidatePlayerUpdate(const Json& p) const {
    if (!Number(p, "sceneNum", 0, SCENE_ID_MAX - 1) || !Number(p, "linkAge", 0, 1) ||
        !Number(p, "currentBoots", 0, PLAYER_BOOTS_MAX - 1) || !Number(p, "currentShield", 0, PLAYER_SHIELD_MAX - 1) ||
        !Number(p, "currentTunic", 0, PLAYER_TUNIC_MAX - 1) || !Number(p, "modelGroup", 0, PLAYER_MODELGROUP_MAX - 1) ||
        !Number(p, "itemAction", -1, PLAYER_IA_MAX - 1) || !Number(p, "heldItemAction", -1, PLAYER_IA_MAX - 1) ||
        !Number(p, "buttonItem0", 0, 255) || !p.contains("posRot") || !Position(p.at("posRot")) ||
        !p.contains("jointTable") || p.at("jointTable").size() != 72 || !ArrayRange(p.at("jointTable"), -32768, 32767) ||
        !p.contains("prevTransl") || !Vector(p.at("prevTransl"), true) ||
        !p.contains("upperLimbRot") || !Vector(p.at("upperLimbRot"), true) ||
        !Number(p, "movementFlags", 0, 255) || !Number(p, "stateFlags1", 0, UINT32_MAX) ||
        !Number(p, "stateFlags2", 0, UINT32_MAX) || !Number(p, "invincibilityTimer", -128, 127) ||
        !Number(p, "unk_862", -GID_MAXIMUM, GID_MAXIMUM) || !Number(p, "actionVar1", -128, 127) ||
        !p.contains("unk_85C") || !p.at("unk_85C").is_number() ||
        !std::isfinite(p.at("unk_85C").get<float>()) || std::abs(p.at("unk_85C").get<float>()) > 1000000) return false;
    return true;
}
bool Session::ValidateSharedEvent(const Json& p) const {
    auto type = p.value("type", std::string{});
    if (type == Anchor::GIVE_ITEM) {
        if (!Number(p, "modId", 0, 1)) return false;
        // ItemTableManager owns the vanilla lookup. Randomizer indices must be
        // bounded before RetrieveItem indexes its static array.
        return Number(p, "getItemId", 1, p.at("modId").get<int>() == MOD_RANDOMIZER ? RG_MAX - 1 : GI_MAX - 1);
    }
    if (type == Anchor::SET_FLAG || type == Anchor::UNSET_FLAG) {
        if (!Number(p, "sceneNum", 0, SCENE_ID_MAX) || !Number(p, "flagType", 0, 32) || !Number(p, "flag", 0, 32767)) return false;
        int scene = p.at("sceneNum").get<int>(), flag = p.at("flag").get<int>(), kind = p.at("flagType").get<int>();
        if (scene != SCENE_ID_MAX) {
            return flag < (kind == FLAG_SCENE_SWITCH || kind == FLAG_SCENE_COLLECTIBLE ? 64 : 32) && (kind == FLAG_SCENE_SWITCH || kind == FLAG_SCENE_CLEAR ||
                                 kind == FLAG_SCENE_COLLECTIBLE || kind == FLAG_SCENE_TREASURE);
        }
        if (kind == FLAG_EVENT_CHECK_INF) return flag < 14 * 16;
        if (kind == FLAG_ITEM_GET_INF) return flag < 4 * 16;
        if (kind == FLAG_INF_TABLE) return flag < 30 * 16;
        if (kind == FLAG_EVENT_INF) return flag < 4 * 16;
        if (kind == FLAG_RANDOMIZER_INF) return flag < RAND_INF_MAX;
        if (kind == FLAG_GS_TOKEN) return ((flag & 0x1F00) >> 8) < 24 && (flag & ~0x1FFF) == 0;
        return false;
    }
    if (type == Anchor::SET_CHECK_STATUS)
        return IS_RANDO && Number(p, "rc", 0, RC_MAX - 1) && Number(p, "status", RCSHOW_UNCHECKED, RCSHOW_SAVED) && p.contains("skipped") && p.at("skipped").is_boolean();
    if (type == Anchor::UPDATE_DUNGEON_ITEMS)
        return Number(p, "mapIndex", 0, 18) && Number(p, "dungeonItems", 0, 7) && Number(p, "dungeonKeys", -1, 127);
    if (type == Anchor::UPDATE_BEANS_COUNT)
        return Number(p, "amount", 0, 10) && Number(p, "amountBought", 0, 10);
    if (type == Anchor::ENTRANCE_DISCOVERED) return Number(p, "entranceIndex", 0, ENTR_MAX - 1);
    return type == Anchor::GAME_COMPLETE;
}

bool Session::ValidateSnapshot(const Json& packet) const {
    try {
        if (!packet.is_object() || !packet.contains("state") || !packet.at("state").is_object()) return false;
        const auto& s = packet.at("state");
        auto reference = Anchor::Instance->BuildTeamState().at("state");
        if (!SameShape(s, reference)) return false;
        if (!Number(s, "healthCapacity", 0x10, 0x140) || !Number(s, "magicLevel", 0, 2) ||
            !Number(s, "magicCapacity", 0, 0x60) || !Number(s, "isMagicAcquired", 0, 1) ||
            !Number(s, "isDoubleMagicAcquired", 0, 1) || !Number(s, "isDoubleDefenseAcquired", 0, 1) ||
            !Number(s, "bgsFlag", 0, 1) || !Number(s, "swordHealth", 0, 127)) return false;
        const auto& inventory = s.at("inventory");
        if (!ArrayRange(inventory.at("items"), 0, 255) || !ArrayRange(inventory.at("ammo"), -1, 127) ||
            !ArrayRange(inventory.at("dungeonItems"), 0, 7) || !ArrayRange(inventory.at("dungeonKeys"), -1, 127) ||
            !Number(inventory, "equipment", 0, 65535) || !Number(inventory, "upgrades", 0, 0x7FFFFF) ||
            !Number(inventory, "questItems", 0, UINT32_MAX) || !Number(inventory, "gsTokens", 0, 100) ||
            !Number(inventory, "defenseHearts", 0, 20)) return false;
        if (!ValidUpgradeLevels(inventory.at("upgrades").get<uint32_t>()) ||
            (inventory.at("questItems").get<uint32_t>() >> 28) > 3) return false;
        for (size_t i = 0; i < inventory.at("items").size(); ++i)
            if (!ValidInventoryItem(i, inventory.at("items")[i].get<uint8_t>())) return false;
        const auto& ship = s.at("ship");
        if (!ArrayRange(ship.at("randomizerInf"), 0, 65535) ||
            !ArrayRange(ship.at("stats").at("entrancesDiscovered"), 0, UINT32_MAX)) return false;
        const auto& random = ship.at("quest").at("data").at("randomizer");
        if (!Number(random, "triforcePiecesCollected", 0, 255) || !Number(random, "bombchuUpgradeLevel", 0, 3)) return false;
        for (const char* flags : { "eventChkInf", "itemGetInf", "infTable" })
            if (!ArrayRange(s.at(flags), 0, 65535)) return false;
        if (!ArrayRange(s.at("sceneFlags"), 0, UINT32_MAX) || !ArrayRange(s.at("gsFlags"), 0, UINT32_MAX)) return false;
        if (s.at("ship").at("quest").at("id") != reference.at("ship").at("quest").at("id")) return false;
        if (s.contains("rando")) {
            for (const auto& location : s.at("rando").at("itemLocations"))
                if (location.size() != 2 || !ArrayRange(location, 0, RCSHOW_SAVED) || location[1].get<int>() > 1) return false;
        }
        return true;
    } catch (...) { return false; }
}
Json Session::MergeSaveOffer(const Json& offer) const {
    auto result = Anchor::Instance->BuildTeamState();
    auto& a = result["state"];
    const auto& b = offer.at("state");
    auto ownCapacity = a.at("healthCapacity").get<uint16_t>();
    auto otherCapacity = b.at("healthCapacity").get<uint16_t>();
    for (const char* key : { "healthCapacity", "magicLevel", "magicCapacity", "isMagicAcquired", "isDoubleMagicAcquired",
                             "isDoubleDefenseAcquired", "bgsFlag" })
        a[key] = std::max(a.at(key).get<int>(), b.at(key).get<int>());
    for (const char* key : { "eventChkInf", "itemGetInf", "infTable", "gsFlags" }) {
        size_t count = a.at(key).size();
        if (std::string(key) == "infTable") --count; // Keep the local swordless row.
        for (size_t i = 0; i < count; ++i) a[key][i] = a[key][i].get<uint32_t>() | b.at(key)[i].get<uint32_t>();
    }
    // Do not import another file's transient puzzle state, enemy-clear state,
    // or dungeon-key balance. The host owns the live world and spent keys.
    for (size_t i = 0; i < a.at("sceneFlags").size(); i += 4) {
        a["sceneFlags"][i] = a["sceneFlags"][i].get<uint32_t>() | b.at("sceneFlags")[i].get<uint32_t>();
        a["sceneFlags"][i + 3] = a["sceneFlags"][i + 3].get<uint32_t>() | b.at("sceneFlags")[i + 3].get<uint32_t>();
    }
    auto& ai = a["inventory"]; const auto& bi = b.at("inventory");
    ai["equipment"] = ai.at("equipment").get<uint16_t>() | bi.at("equipment").get<uint16_t>();
    ai["upgrades"] = MergeUpgrades(ai.at("upgrades").get<uint32_t>(), bi.at("upgrades").get<uint32_t>());
    ai["questItems"] = MergeQuestItems(ai.at("questItems").get<uint32_t>(), bi.at("questItems").get<uint32_t>(), ownCapacity, otherCapacity);
    ai["gsTokens"] = std::max(ai.at("gsTokens").get<int>(), bi.at("gsTokens").get<int>());
    ai["defenseHearts"] = std::max(ai.at("defenseHearts").get<int>(), bi.at("defenseHearts").get<int>());
    for (size_t i = 0; i < 18; ++i) ai["items"][i] = MergePermanentItem(i, ai["items"][i].get<uint8_t>(), bi.at("items")[i].get<uint8_t>());
    for (size_t i = 0; i < ai.at("dungeonItems").size(); ++i)
        ai["dungeonItems"][i] = ai["dungeonItems"][i].get<uint8_t>() | bi.at("dungeonItems")[i].get<uint8_t>();
    auto& flags = a["ship"]["randomizerInf"];
    for (size_t i = 0; i < flags.size(); ++i) flags[i] = flags[i].get<uint16_t>() | b.at("ship").at("randomizerInf")[i].get<uint16_t>();
    auto& entrances = a["ship"]["stats"]["entrancesDiscovered"];
    for (size_t i = 0; i < entrances.size(); ++i)
        entrances[i] = entrances[i].get<uint32_t>() | b.at("ship").at("stats").at("entrancesDiscovered")[i].get<uint32_t>();
    if (IS_RANDO) {
        auto& random = a["ship"]["quest"]["data"]["randomizer"];
        for (const char* key : { "triforcePiecesCollected", "bombchuUpgradeLevel" })
            random[key] = std::max(random.at(key).get<int>(), b.at("ship").at("quest").at("data").at("randomizer").at(key).get<int>());
    }
    if (a.contains("rando")) {
        auto& locations = a["rando"]["itemLocations"];
        for (size_t i = 0; i < locations.size(); ++i)
            locations[i][0] = std::max(locations[i][0].get<int>(), b.at("rando").at("itemLocations")[i][0].get<int>());
    }
    return result;
}
void Session::ApplySharedState(const Json& packet) {
    if (!active || !shareProgress || !Anchor::Instance->IsSaveLoaded()) return;
    if (!ValidateSnapshot(packet)) throw std::runtime_error("Shared save validation failed; local save was not changed");
    SaveContext remote{};
    packet.at("state").get_to(remote);
    applying = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{ applying };
    // Character identity, file number, age, entrance, equips, rupees, current
    // health/magic, ammo, bottle contents, trades, playtime and timestamps stay
    // local. Never assign gSaveContext or the entire inventory from the network.
    auto ownCapacity = gSaveContext.healthCapacity;
    gSaveContext.healthCapacity = std::max(gSaveContext.healthCapacity, remote.healthCapacity);
    gSaveContext.magicLevel = std::max(gSaveContext.magicLevel, remote.magicLevel);
    gSaveContext.magicCapacity = std::max(gSaveContext.magicCapacity, remote.magicCapacity);
    gSaveContext.isMagicAcquired |= remote.isMagicAcquired;
    gSaveContext.isDoubleMagicAcquired |= remote.isDoubleMagicAcquired;
    gSaveContext.isDoubleDefenseAcquired |= remote.isDoubleDefenseAcquired;
    gSaveContext.bgsFlag |= remote.bgsFlag;
    auto& local = gSaveContext.inventory;
    for (size_t i = 0; i < 18; ++i) local.items[i] = MergePermanentItem(i, local.items[i], remote.inventory.items[i]);
    local.equipment |= remote.inventory.equipment;
    local.upgrades = MergeUpgrades(local.upgrades, remote.inventory.upgrades);
    local.questItems = MergeQuestItems(local.questItems, remote.inventory.questItems, ownCapacity, remote.healthCapacity);
    local.gsTokens = std::max(local.gsTokens, remote.inventory.gsTokens);
    local.defenseHearts = std::max(local.defenseHearts, remote.inventory.defenseHearts);
    for (size_t i = 0; i < ARRAY_COUNT(local.dungeonItems); ++i) local.dungeonItems[i] |= remote.inventory.dungeonItems[i];
    for (size_t i = 0; i < ARRAY_COUNT(local.dungeonKeys); ++i) local.dungeonKeys[i] = remote.inventory.dungeonKeys[i];
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.sceneFlags); ++i) {
        auto& a = gSaveContext.sceneFlags[i]; const auto& b = remote.sceneFlags[i];
        a.chest |= b.chest;
        a.collect |= b.collect;
        // Import persistent world progress, preserving the engine's explicitly
        // local switch exceptions and never touching temporary flags.
        uint32_t keep = 0;
        if (i == SCENE_WATER_TEMPLE) keep = (1u << 0x1C) | (1u << 0x1D) | (1u << 0x1E);
        if (i == SCENE_FOREST_TEMPLE) keep = 1u << 0x1B;
        if (i == SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR) keep = 1u << 0x17;
        a.swch = (b.swch & ~keep) | (a.swch & keep);
        a.clear = b.clear;
        if (gPlayState->sceneNum == static_cast<s16>(i)) {
            gPlayState->actorCtx.flags.chest |= a.chest;
            gPlayState->actorCtx.flags.collect |= a.collect;
            gPlayState->actorCtx.flags.swch = (gPlayState->actorCtx.flags.swch & keep) | (a.swch & ~keep);
            gPlayState->actorCtx.flags.clear = a.clear;
        }
    }
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.eventChkInf); ++i) gSaveContext.eventChkInf[i] |= remote.eventChkInf[i];
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.itemGetInf); ++i) gSaveContext.itemGetInf[i] |= remote.itemGetInf[i];
    for (size_t i = 0; i + 1 < ARRAY_COUNT(gSaveContext.infTable); ++i) gSaveContext.infTable[i] |= remote.infTable[i];
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.gsFlags); ++i) gSaveContext.gsFlags[i] |= remote.gsFlags[i];
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.ship.randomizerInf); ++i) gSaveContext.ship.randomizerInf[i] |= remote.ship.randomizerInf[i];
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.ship.stats.entrancesDiscovered); ++i)
        gSaveContext.ship.stats.entrancesDiscovered[i] |= remote.ship.stats.entrancesDiscovered[i];
    if (IS_RANDO) {
        auto& localRando = gSaveContext.ship.quest.data.randomizer;
        const auto& remoteRando = remote.ship.quest.data.randomizer;
        localRando.triforcePiecesCollected = std::max(localRando.triforcePiecesCollected, remoteRando.triforcePiecesCollected);
        localRando.bombchuUpgradeLevel = std::max(localRando.bombchuUpgradeLevel, remoteRando.bombchuUpgradeLevel);
    }
    if (IS_RANDO && packet.at("state").contains("rando")) {
        auto context = Rando::Context::GetInstance();
        const auto& locations = packet.at("state").at("rando").at("itemLocations");
        for (int i = 0; i < RC_MAX; ++i) {
            auto* location = context->GetItemLocation(i);
            auto incoming = locations[i][0].get<RandomizerCheckStatus>();
            if (incoming > location->GetCheckStatus()) location->SetCheckStatus(incoming);
        }
        CheckTracker::RecalculateAllAreaTotals();
        CheckTracker::RecalculateAvailableChecks();
    }
}
} // namespace Shipwright::Direct
