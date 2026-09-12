# Multiplayer acceptance checklist — NOT YET EXECUTED

Use copies of saves, identical builds and two separate save directories/PCs. Record the executable/source revision, operating systems, LAN addresses, port, quest/seed/settings and chosen slots. This checklist is a release gate, not a statement that these checks have passed.

## Build and offline regression

- [ ] Compile the complete game with the supplied dependency/toolchain instructions, including the new menu and game adapter. No new compiler errors or unresolved Winsock symbols.
- [ ] Generate the normal `soh.o2r` build asset and start with supported game data.
- [ ] Start in Offline mode. Create, save, quit and reload all three ordinary slots without changing their ownership.
- [ ] Existing single-player menus, gameplay, controllers, autosave and save states work when direct multiplayer is inactive.
- [ ] A previously used legacy Anchor connection can still be enabled, disabled and used independently. Starting both connections concurrently is blocked.

## Personal saves on two PCs

- [ ] Prepare visibly different inventories, player names and progress. Use host slot 1 and joiner slot 2 to catch accidental slot copying.
- [ ] Host & Play reports Hosting. Join by numeric LAN IP/port reports Joined only after its initial roster is received.
- [ ] Both players are visible in the same scene; movement, rotation through -32768 yaw, animation, age/model, equipped items and name labels agree with the owning player.
- [ ] Existing controls remain local to the correct player. Remote actors do not appear at origin or with stale first-frame poses.
- [ ] Walking to another scene/room, returning, dying, respawning and changing age do not silently disconnect or leave duplicate/stale remote actors.
- [ ] Each player's sounds/ocarina notes route to the intended same-scene peers. Teleporting works in permitted scenes and stays blocked in the baseline's excluded scenes.
- [ ] Pick up items/open chests in personal mode. No remote inventory, quest flag, rupee balance, HP, or save file is changed by those pickups.
- [ ] Save & Leave, restart each game, and confirm that the different files/slots are still distinct. Other local slots match their pre-test hashes.

## Shared adventure and restoration

- [ ] Enable sharing on the host; a joiner without explicit permission is rejected before shared progress changes its game.
- [ ] Both sides get non-overwriting, readable backups before connection; restore a copy offline and verify it loads.
- [ ] Read-only/unwritable backup directory and missing on-disk save prevent the session from starting.
- [ ] Share matching ordinary saves at different progression levels. Permanent tiers merge without downgrading or manufacturing higher tiers.
- [ ] Check heart totals and heart-piece fragments, empty permanent inventory slots, equipment, songs/medallions, map/compass/boss-key flags, discoveries and chest flags after initial join and explicit resync.
- [ ] Initial/resync snapshots retain each local identity, slot, age, entrance, equipment buttons, rupees, current HP/MP, ammo, bottles, trades, playtime and timestamps.
- [ ] Validate live shared awards separately: normal items, bottles/trades, consumables, heart pieces, great fairy upgrades, bean events and randomizer traps use the intended cooperative policy.
- [ ] Test simultaneous identical pickup, key pickup/spend, different players opening the same door, and resync immediately afterward. Any duplication or balance loss is a release blocker; there is no implemented global pickup ledger or authoritative transaction system.
- [ ] Test puzzle switches in Water Temple, Forest Temple and Ganon's Tower collapse; confirm the baseline's local exceptions do not softlock progress.
- [ ] Test matching randomizer seeds/settings, including one-heart starts, progressive upgrades, checks, triforce counts and entrance discoveries. Check metadata tracks item ownership correctly.
- [ ] Mismatched quest, build, seed or settings is rejected without applying a remote snapshot. Boss Rush is refused.
- [ ] Save/autosave both local files, close, reopen and verify the intended shared progress persisted only in their selected slots.

## Lifecycle, reliability and limits

- [ ] Arm launch from file select, cancel it, then arm again and load a file. The correct launch occurs exactly once.
- [ ] Invalid/blank IP/name, out-of-range port, occupied host port, unreachable/refusing host and a full session produce recoverable UI states.
- [ ] Host and multiple clients join/leave/rejoin repeatedly. No duplicate actors, lingering listener or lost local controls.
- [ ] Return to file select/change slots during a session; disconnect happens before loading another file. F5/F7 and console save-state requests are blocked while active, including requests queued just before launch.
- [ ] Client quits, host quits, host process is killed, network cable is interrupted and slow/stalled peers are simulated. Other save files remain readable; remaining clients can disconnect and continue locally.
- [ ] Test three participants and then all eight; check ordering and performance during scene transitions and item events.
- [ ] Perform a sustained play/save/rejoin soak on each intended OS. Check memory/thread counts and normal frame timing.

## Scope expectations

Enemy AI/health, boss combat, projectile authority, physics and cutscene timing are local simulations in this preview. Do not mark them as synchronized because the player models or completion flags match. A fully shared-enemy co-op conversion requires additional implementation and separate acceptance tests.

Tester / build / date: ____________________  
Results and unresolved issues: ____________________  
Release accepted: **No — awaiting full build and gameplay validation.**
