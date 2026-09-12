#include "DirectMultiplayer.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SaveManager.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
extern PlayState* gPlayState;
}
namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
extern std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;
}
namespace Shipwright::Direct {
namespace {
void Persist() { Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame(); }
void DrawMultiplayer(WidgetInfo&) {
    auto& session = Session::Get();
    ImGui::SeparatorText("Direct-IP Multiplayer");
    ImGui::TextWrapped("Host a session inside this game, or join by typing the host's IPv4 address. "
                       "Each player loads their own normal save slot; no external server program is needed.");
    ImGui::Spacing();
    ImGui::BeginDisabled(session.Active());
    int mode = CVarGetInteger(CVAR_DIRECT("LaunchMode"), 0);
    if (mode < 0 || mode > 2) mode = 0;
    const char* modes[] = { "Offline / Single Player", "Host & Play", "Join by IP" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##DirectLaunchMode", &mode, modes, 3)) {
        CVarSetInteger(CVAR_DIRECT("LaunchMode"), mode); Persist();
    }
    std::string playerName = CVarGetString(CVAR_DIRECT("Name"), "Link");
    ImGui::TextUnformatted("Player name (1-32 bytes)");
    if (UIWidgets::InputString("##DirectName", &playerName, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_DIRECT("Name"), playerName.c_str()); Persist();
    }
    std::string address = CVarGetString(CVAR_DIRECT("Host"), "127.0.0.1");
    if (mode == 2) {
        ImGui::TextUnformatted("Host IPv4 address");
        if (UIWidgets::InputString("##DirectHost", &address, UIWidgets::InputOptions().Color(THEME_COLOR))) {
            CVarSetString(CVAR_DIRECT("Host"), address.c_str()); Persist();
        }
    }
    int port = CVarGetInteger(CVAR_DIRECT("Port"), DefaultPort);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::InputInt("TCP port##DirectPort", &port, 0)) {
        CVarSetInteger(CVAR_DIRECT("Port"), port); Persist();
    }
    bool share = CVarGetInteger(CVAR_DIRECT("ShareProgress"), 0) != 0;
    if (ImGui::Checkbox(mode == 2 ? "Allow shared progress to update my selected save" : "Share adventure progress", &share)) {
        CVarSetInteger(CVAR_DIRECT("ShareProgress"), share); Persist();
    }
    if (share) {
        ImGui::TextWrapped("Shared mode merges permanent items and completion flags from compatible saves. "
                           "The host supplies live switches and dungeon-key balances on join/resync. Your save slot, "
                           "character identity and timestamps are not replaced. Initial/resync snapshots keep personal "
                           "resources, bottle contents and trade slots; live shared pickups can still award items, "
                           "resource effects and quest changes. Shared progress is written when you save or autosave.");
    } else {
        ImGui::TextWrapped("Personal-save mode: players see each other, their animations and equipment, and can "
                           "teleport to each other. Inventory and world progress are not copied between saves.");
    }
    ImGui::TextWrapped("A dated backup is made before connecting. Existing Anchor connections must be disabled first. "
                       "Both PCs must run this exact multiplayer build and compatible quest/seed settings.");
    ImGui::Spacing();
    if (mode != 0 && ImGui::Button("Launch selected mode", ImVec2(-1, 0))) {
        session.Start(static_cast<LaunchMode>(mode), address, port, playerName, share);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::SeparatorText("Session Status");
    ImGui::TextWrapped("%s", session.Status().c_str());
    if (session.Active()) {
        if (session.WaitingForSave()) {
            ImGui::TextWrapped("Close this menu and load your own save from file select. Multiplayer starts after it loads.");
            if (ImGui::Button("Cancel launch", ImVec2(-1, 0))) session.Stop("Launch cancelled. Offline.");
        } else {
            if (session.Ready() && ImGui::Button("Save & Leave Session", ImVec2(-1, 0))) {
                if (Anchor::Instance->IsSaveLoaded()) {
                    Play_PerformSave(gPlayState);
                    SaveManager::Instance->ThreadPoolWait();
                }
                session.Stop("Saved your selected local file and left the session.");
            }
            if (ImGui::Button(session.Ready() ? "Disconnect (keep playing locally)" : "Cancel connection", ImVec2(-1, 0)))
                session.Stop("Disconnected. In-memory progress remains local; save normally to retain it.");
        }
    }
    if (!session.BackupPath().empty()) {
        ImGui::TextWrapped("Backup: %s", session.BackupPath().c_str());
        if (ImGui::Button("Copy backup path")) ImGui::SetClipboardText(session.BackupPath().c_str());
    }
    if (session.Ready()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Players");
        SohGui::mAnchorRoomWindow->DrawElement();
        if (session.SharingProgress() && ImGui::Button("Resync shared progress")) Anchor::Instance->SendPacket_RequestTeamState();
    }
}
void DrawHelp(WidgetInfo&) {
    ImGui::SeparatorText("Two-PC Setup");
    ImGui::TextWrapped("HOST: Select Host & Play, choose a player name and port, then Launch. Load your own save "
                       "normally. Wait for the Hosting status before inviting the other player.");
    ImGui::Spacing();
    ImGui::TextWrapped("JOINER: Select Join by IP and enter the host PC's LAN IPv4 address, such as 192.168.1.20. "
                       "Use the same port, launch, and load your own save. 127.0.0.1 is only for two instances on one PC.");
    ImGui::Spacing();
    ImGui::TextWrapped("Allow the host executable through the firewall for TCP on the selected port. Internet play "
                       "requires a reachable host address and port forwarding or a trusted private VPN; there is no NAT relay.");
    ImGui::Spacing();
    ImGui::SeparatorText("Save Safety");
    ImGui::TextWrapped("Normal, Master Quest, and randomizer files are kept distinct. Randomizer seed and settings "
                       "must match; this does not distribute spoiler/seed files. Other local save slots are never replaced. "
                       "Changing files ends the connection first. Save states are blocked during sessions. "
                       "Do not open the same save directory in two processes.");
    ImGui::Spacing();
    ImGui::SeparatorText("Replication Scope");
    ImGui::TextWrapped("Uses this baseline's Anchor player poses, animations, equipment, sounds, teleporting and "
                       "optional cooperative item/world-flag synchronization. Enemy AI, boss combat, physics, projectiles "
                       "and cutscene timing are still simulated locally. Simultaneous pickups/key use need in-game validation. "
                       "This is not a lockstep or fully authoritative "
                       "shared-enemy simulation. PvP is disabled in direct sessions.");
    ImGui::Spacing();
    ImGui::TextWrapped("Private, trusted-player sessions only. The connection is not encrypted or password-authenticated. "
                       "The host controls the session; leaving does not migrate hosting to another player.");
}
void RegisterMenu() {
    WidgetPath path{ "Network", "Multiplayer", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "Direct Multiplayer", WIDGET_CUSTOM).CustomFunction(DrawMultiplayer).HideInSearch(true);
    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "Direct Multiplayer Help", WIDGET_CUSTOM).CustomFunction(DrawHelp).HideInSearch(true);
}
static RegisterMenuInitFunc registration(RegisterMenu);
} // namespace
} // namespace Shipwright::Direct
