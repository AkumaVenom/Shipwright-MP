# Consolidated repository packaging — 12 September 2026

This packaging revision combines helper 1.2 and its two compile corrections with the Direct-IP source preview. It removes stale submodule pointers, corrects fresh-import ignore rules, separates inactive upstream workflow examples, preserves attribution, and adds GitHub Desktop publishing instructions. Source fonts are obtained separately with original-content SHA-256 checks. No new multiplayer gameplay capability or successful full game build is claimed.

# Multiplayer change log

## 1.0.0 implementation preview — 12 September 2026

Based only on the supplied `Shipwright-MP.zip`; upstream project version 9.2.3 is retained. This custom edition is not represented as an official Harbour Masters release.

### Added

- Network → Multiplayer page with Offline, Host & Play and Join by IP launch selection; names, numeric IPv4, port, progression policy, status and cancellation/leave controls.
- Embedded eight-participant host relay, length-framed non-blocking native TCP, bounded byte/count queues, partial read/write handling, orderly rejection flush, timeouts, heartbeat and reconnect cleanup.
- Build/quest/randomizer identity checks, consent-gated shared progression, sender/target routing validation and roster/snapshot readiness barrier.
- Selected-save on-disk backups and diagnostic entry snapshots; selective permanent progression merge with correct packed upgrade tiers and heart totals. Local ownership, identity and other slots are not replaced.
- 21-case standalone transport/merge test executable, 19 static integration contracts, exact validation logs, scope documentation and a two-PC acceptance checklist.

### Integrated / hardened

- Existing Anchor player, sound, teleport and cooperative progression handlers route through the direct session without a separate external Anchor server.
- First-pose rendering guards, client-state initialization and scene-change pose invalidation reduce uninitialized/stale remote visuals.
- Save-worker callbacks request game-thread snapshots instead of reading live game state from the background save worker.
- Save-slot changes disconnect before loading; pending and new save-state operations are blocked during a direct launch/session.
- Engine shutdown joins the direct transport before teardown; legacy Anchor and direct connections are mutually exclusive.
- Direct frame processing avoids modifying the currently iterated game-frame hook map. The legacy client-version filter runs on the game thread rather than reading the client map on its network worker.

### Not implemented / not verified

- No shared enemy/boss authority, deterministic world simulation, projectile/physics/cutscene replication, PvP, host migration, NAT relay, encryption, password authentication or global pickup deduplication.
- Cooperative live item/key/flag handling remains based on the baseline's Anchor behaviour. Simultaneous actions and quest interactions require game acceptance tests.
- Full-game compilation and rendered/two-PC gameplay were not possible in this environment. The final native tests passed with GCC/Clang and sanitizers; they do not certify the game adapter or save integration.
