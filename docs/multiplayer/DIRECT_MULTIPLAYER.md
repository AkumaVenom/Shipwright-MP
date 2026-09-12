# Direct-IP Multiplayer — implementation preview 1.0.0

Date: 12 September 2026  
Baseline: the supplied `Shipwright-MP.zip` (Ship project 9.2.3)  
Entry point: **Esc → Network → Multiplayer**

## Release status — read first

This is a **modified source baseline**, not a prebuilt game or a release-qualified full multiplayer conversion. The new native socket transport and pure save-merge helpers were compiled and exercised with GCC and Clang, including AddressSanitizer and UndefinedBehaviorSanitizer. The game adapter, menu, rendering, normal save integration and two-PC gameplay **have not been compiled or runtime-validated in this environment**. Full-game configuration reached dependency acquisition and failed because the environment could not resolve GitHub to fetch spdlog. No game ROM or extracted runtime assets were available for a playtest.

The implementation reuses the uploaded baseline's Anchor player and cooperative progression systems. **Enemy AI, shared enemy/boss health, projectiles, combat authority, physics, cutscene timing, deterministic world simulation and simultaneous-pickup arbitration are not implemented.** This is a direct-host transport and save-integration preview, not a claim that every gameplay system now has authoritative multiplayer. Do not replace your accepted stable build until the acceptance checklist passes.

## Host and join

Use the identical modified build on every PC. Each player should first load their own normal save slot and save locally. The host can be in a different slot number from a joining player. Disable the older Anchor connection before starting a direct session.

On the host PC, open **Network → Multiplayer**, select **Host & Play**, choose a player name and a TCP port (default **43384**), and choose the progression policy below. Press **Launch selected mode**. The status must report **Hosting** before other players connect. Hosting is inside the game process; no separate Anchor server is required.

On the joining PC, select **Join by IP**, enter the host's numeric IPv4 address, use the same TCP port, choose a name and press **Launch selected mode**. For example, a LAN host may have an address such as `192.168.1.20`. `127.0.0.1` refers to the same computer and is only appropriate for local multi-instance tests. Hostnames, IPv6 and `IP:port` strings are not accepted; the port has a separate field.

The launch button also works at file select: it arms the selected mode and waits for the player to load their own file normally. Merely choosing the dropdown does not open a socket. Restarting the application does not automatically reconnect. An armed launch can be cancelled.

The protocol accepts up to **eight participants, including the host**. A full listener closes additional connections. Duplicate-port, invalid-input, connection, version/quest/seed, sharing-consent and handshake failures leave the local game available rather than selecting or replacing a remote save slot.

### Connectivity

For LAN play, allow the host executable through the local firewall for inbound TCP on the chosen port. Other players need a route to that host. For Internet play, use a reachable public IPv4 address with the appropriate host-side TCP port forwarding, or a trusted private VPN with reachable IPv4 addresses. There is **no automatic NAT traversal, relay service, UPnP, matchmaking or account service**. This protocol does not have encryption or password authentication; use it only with trusted players on a trusted/restricted network. Do not expose it as a public server.

On Windows, the host can inspect its active network adapter with `ipconfig`. Do not give a joining LAN player a loopback address or the gateway/router's address. Network configuration is outside the game; the application does not modify firewall/router settings.

## Save ownership and progression policies

### Personal-save mode — default

Leave **Share adventure progress** off on the host. Each participant keeps their own progression and local save file. Player poses, visible equipment, animation data, sounds, location state and supported teleport requests are routed, but inventory and world-progress mutations are not shared. A player further through the story does not overwrite a newcomer’s inventory. No save snapshot is broadcast in this mode.

### Shared adventure — explicit consent

Enable **Share adventure progress** on the host. Joining players must enable **Allow shared progress to update my selected save**; otherwise the host rejects the connection with an explanation. A host in personal mode remains personal even when a joining player has enabled that permission.

Each player still loads and saves to their own selected local slot. Instead of uploading an arbitrary `.sav` file, the joiner sends a bounded, typed view of the progression in the currently loaded file. The host validates its shape and relevant value ranges, merges compatible permanent progression, and sends the resulting shared state. The initial handshake checks protocol, custom build identity, source revision, quest type, and randomizer seed/settings. Normal, Master Quest and randomizer quests are not mixed; Boss Rush is refused.

The host takes the greater valid progression for permanent item tiers and certain counters, combines completion flags, and supplies its current scene-switch/clear state and dungeon-key balances on join and resync. Newcomers do not import their old dungeon-key balances or transient puzzle state into the host. Packed equipment upgrades are merged per tier, not bitwise OR; heart-piece progress follows the save with the greater total health progress. These rules avoid manufacturing a third-tier upgrade from first- and second-tier upgrades or adding an older save’s heart fragments on top of a more advanced health total.

Initial/resync snapshots **do not copy** a remote slot number, character name, local age, entrance, equipped buttons, rupees, current health/magic, ammunition, bottle contents, trade slots, playtime or save timestamps. They do merge selected permanent inventory/progression, scene completion flags, randomizer discovery/progress, and host dungeon-key state. A resync is not a rollback or a complete remote inventory replacement.

**Live cooperative events retain the baseline’s Anchor item behaviour.** A remote pickup can award an item or resource effect, apply an ice trap, update beans or affect quest progression. The snapshot protection of consumables/trades is not a promise that live shared item awards never affect those fields. Independent scene-local enemy simulation and concurrent item/key operations can still conflict. The current dungeon-key event path is based on the baseline’s reported balances, not an authoritative inventory transaction service. Avoid simultaneous unique pickups/key spending until this path has been validated or extended.

Shared changes stay in memory after disconnecting and become part of that player’s selected file on a normal save or autosave. Disconnecting does **not** restore the entry backup. Old/offline saves brought into later sessions may reintroduce previously recorded permanent flags by union; the system does not maintain a global item-origin ledger or deduplicate concurrent pickups by world-check identity.

### Backups

Before hosting or joining begins, the game waits for outstanding local save writes and copies the selected on-disk save, without overwriting an existing backup, to:

```text
<application save root>/Save/MultiplayerBackups/<timestamp>-slotN/fileN.sav
```

The UI shows the actual full backup path and has a copy-path button. Other save slots are not copied into the session or replaced. If there is no existing local save, or the backup/diagnostic snapshot cannot be written, launch fails closed.

The `.sav` backup contains the **last completed disk save**, not necessarily unsaved gameplay since then. The neighbouring `session-entry-progress.json` records the currently loaded progression as diagnostics, but it is **not a complete save file and cannot be renamed to `.sav` to restore the game**. Save normally before launching when you need a fully restorable checkpoint of your present state.

To restore, leave the session, close the game completely, preserve a separate copy of the current affected save, and copy the backed-up `fileN.sav` back to its original slot path. Restart the game after restoration. No automatic rollback or destructive restore button is included.

### Leaving, changing files and save states

**Save & Leave Session** uses the normal game save routine, waits for the save worker, and disconnects. **Disconnect (keep playing locally)** closes the session without explicitly saving or undoing progress; existing autosave behaviour still applies. If the host leaves or crashes, clients disconnect and retain their current local game state; there is no host migration.

Changing a normal file disconnects before SaveManager takes its load mutex. Ordinary scene/entrance transitions are not treated as a file change. Save-state creation/loading (including F5/F7 and console requests) is blocked while a direct launch/session is active because save states restore raw actor/system memory and cannot safely restore a live network session. Normal game saves remain available.

Use separate application/save directories for two instances on one computer. Two processes writing the same save/config directory are not supported. Windows/Linux/macOS application save-root conventions still come from the supplied baseline.

## Replication matrix

| System | Implemented path | Verification in this package |
|---|---|---|
| Embedded host, numeric IPv4 join, capacity, framing, partial reads/writes | New bounded non-blocking native TCP transport | Native loopback tests on Linux, GCC + Clang, sanitizers |
| Launch dropdown, connection status, leave controls, backup-path UI | New Network → Multiplayer page | Source integration checks only |
| Player pose, animation joints, age/equipment model state | Existing Anchor player packets with relay validation and first-pose visibility guard | Not rendered or two-PC tested |
| Player/ocarina sounds, supported teleport requests | Existing Anchor handlers with sender/target checks and short-lived teleport correlation | Not audio/gameplay tested |
| Personal progression isolation | Shared events/snapshots gated off by default | Static integration checks; runtime checklist outstanding |
| Shared item/flag/check/entrance progress | Existing Anchor event handlers plus selective join/resync merge | Pure merge helpers tested; gameplay/save integration outstanding |
| Save backups, slot change teardown, Save & Leave | Normal local SaveManager integration | Source integration checks only |
| Enemy/boss authority, projectiles, physics, cutscenes, simultaneous pickup arbitration | **Not implemented** | Not applicable |
| PvP, host migration, encryption, authentication, NAT relay | **Not implemented**; direct-session PvP is disabled | Not applicable |

## Architecture notes

`Transport` owns sockets only on its worker thread. It does not read game actors, CVar settings, game hooks or `gSaveContext`. A four-byte big-endian length prefix frames each JSON payload. Frames are limited to 512 KiB, queues to bounded counts/bytes, and per-peer inactivity/stalled writes trigger disconnection. The normal poll sleeps when idle rather than busy-spinning. IPv4 parsing is numeric and decimal, including leading-zero components.

`Session` owns admission, the embedded relay, the roster and save policy on the game thread. Client-supplied sender IDs are removed; the host stamps the socket-assigned identity. Arbitrary admin/roster/full-save updates from joined clients and PvP damage are not relayed. Packet shape/value bounds, compatibility, admission deadlines, heartbeat handling and a roster/snapshot completion barrier protect the entry path. This is validation and routing, **not server authority over all gameplay outcomes**.

The legacy Anchor connection remains a separate option. Direct sessions bypass its blocking SDL_net transport and use its existing rendering/gameplay hooks. Those hooks do not register another game-frame callback while the current direct callback is iterating the hook registry. The existing OnSaveFile callback now requests a snapshot atomically; the snapshot is built from live game state on the subsequent game thread rather than inside the save worker.

See [VALIDATION.md](VALIDATION.md) for actual automated results and [ACCEPTANCE_CHECKLIST.md](ACCEPTANCE_CHECKLIST.md) for the required game acceptance pass.
