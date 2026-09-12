#!/usr/bin/env python3
"""Static integration assertions only; these are NOT a game compile or gameplay test."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[2]
def source(name):
    return (ROOT / name).read_text(encoding="utf-8")
direct = source("soh/soh/Network/Direct/DirectMultiplayer.cpp")
menu = source("soh/soh/Network/Direct/Menu.cpp")
anchor = source("soh/soh/Network/Anchor/Anchor.cpp")
hooks = source("soh/soh/Network/Anchor/HookHandlers.cpp")
save = source("soh/soh/SaveManager.cpp")
main = source("soh/soh/OTRGlobals.cpp")
states = source("soh/soh/Enhancements/savestates.cpp")
team = source("soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp")
checks = {
    "Network > Multiplayer sidebar exists": 'AddSidebarEntry("Network", "Multiplayer", 2)' in source("soh/soh/SohGui/SohMenuNetwork.cpp"),
    "Offline / Host / Join selector": all(x in menu for x in ['"Offline / Single Player"','"Host & Play"','"Join by IP"']),
    "No automatic direct connection on startup": 'Session::Get().Initialize()' in main and 'Session::Get().Start(' not in main,
    "Shutdown before audio teardown": main.index('Session::Get().Shutdown()') < main.index('OTRAudio_Exit();'),
    "File change disconnect before save mutex": save.index('PrepareForSaveChange()') < save.index('saveMtx.lock();',save.index('void SaveManager::LoadFile')),
    "Backup before transport start": direct.index('if (!BackupSelectedSave())') < direct.index('transport.Host(port)'),
    "Backups do not overwrite": 'std::filesystem::copy_options::none' in direct,
    "Share opt-in defaults off": 'CVAR_DIRECT("ShareProgress"), 0' in menu,
    "Personal snapshot path is gated": 'if (!active || !shareProgress' in direct,
    "Whole SaveContext and inventory are never assigned remotely": not re.search(r'gSaveContext\s*=|gSaveContext\.inventory\s*=',direct),
    "Identity / seed / settings checked": all(x in direct for x in ['"identity"', '"quest"', '"seed"', '"settings"', 'RSK_MAX']),
    "Join completion waits for roster / snapshot": '!receivedRoster || (shareProgress && !receivedSnapshot)' in direct,
    "No direct registration during same hook-map iteration": 'isConnected && !Shipwright::Direct::Session::Get().Active()' in hooks,
    "Save worker requests main-thread snapshot": 'saveSnapshotRequested = true;' in hooks and 'saveSnapshotRequested.exchange(false)' in hooks,
    "Existing Anchor sends route through direct transport": 'Session::Get().Send(std::move(payload))' in anchor,
    "Snapshot uses direct selective merge": 'Session::Get().ApplySharedState(payload)' in team,
    "Windows socket library linked": 'target_link_libraries(soh PRIVATE ws2_32)' in source('soh/CMakeLists.txt'),
    "Save-state request and execution paths both gated": states.count('Shipwright::Direct::Session::Get().Active()') == 2,
    "Scope limits visible in UI": all(x in menu for x in ['Enemy AI', 'boss combat', 'PvP is disabled']),
}
for name, success in checks.items():
    print(('PASS ' if success else 'FAIL ') + name)
print(f'{sum(checks.values())} / {len(checks)} static contracts passed (not compilation).')
raise SystemExit(0 if all(checks.values()) else 1)
