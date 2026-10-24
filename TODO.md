# terraria-3d - Implementation Plan

First-person voxel sandbox with Terraria-style progression (depth layers, biomes, bosses, town NPCs, invasions).
Server-authoritative multiplayer over **Steam networking** (no port forwarding). Rendering by **compute-shader voxel ray marching** (no triangle meshes for terrain).

**Decisions already made**
- First-person, voxel world, Daxa (Vulkan), C++20, CMake + FetchContent.
- Milestone 1 = walk / mine / place + **2 players over LAN/Steam**, before any survival content.
- Content is data-driven (JSON in `data/`), plus an in-game ImGui dev/world editor.
- Platforms: Linux, Windows, Steam Deck (so: GPU budget matters, controller support, Vulkan feature fallbacks).
- Networking: **client <-> host server** (host is authoritative; not peer-to-peer gameplay). Steam Networking Sockets routes connections through Valve's relay by SteamID (no port forwarding). ENet is the dev/dedicated-server/LAN transport behind the same interface. **Implemented, see `src/net/README.md`.**
- Terrain rendering: ray marching voxels in compute, not mesh generation, using a **chunked sparse voxel octree (SVO)**.
- Entities / items / mobs / player models: also voxels, in **object space** (own voxel grid + transform, so rotation works), ray marched.
- No background wall layer (3D has no use for it).
- Art and a custom asset file format: made by David.
- World size: fixed test size now, **infinite world later** - keep chunk addressing, streaming and the page table independent of a fixed world size.

**Legend:** `[ ]` todo, `[x]` done, **YOU** = needs you personally (accounts, assets, decisions, hardware), **DECIDE** = open question with a recommended default.

---

## 0. Where we are

- [x] Project builds (Daxa, GLFW, ImGui, GLM, cgltf, ENet fetched by CMake; clang/ninja)
- [x] Window + Daxa swapchain + clear-screen frame
- [x] Module skeleton with design comments (`src/{core,platform,render,world,physics,entity,game,net,ui,audio}`)
- [x] Network layer: `NetServer`/`NetClient`/`NetworkManager`, typed packets, loopback test passing (ENet)
- [ ] Skeleton comments for **meshing** and **chunk mesh rendering** are now obsolete (see Phase 2 and 3, task "rewrite skeleton docs")

---

## Phase 1 - Foundations (do first, everything depends on these)

### 1.1 Core
- [ ] `core/types.hpp`: add `std::hash<ivec3>`, BlockPos/ChunkPos helpers (floor-div/mod that handle negatives), `ChunkPos <-> index` helpers
- [ ] Logger: levels + tags, file output (replace `core/log.hpp` body; keep call sites)
- [ ] Frame timing: delta time, fixed-timestep accumulator for server tick, FPS counter
- [ ] Job system (`core/job_system.hpp`): thread pool, priorities, completed-results queue drained on main thread, cancellation by generation counter
- [ ] Assertions / error handling policy (`ASSERT`, fatal vs recoverable), crash log with last log lines
- [ ] Settings load/save (JSON in XDG config dir / `%APPDATA%`): `ClientSettings`, `ServerSettings`
- [ ] Command line parsing (`--server`, `--connect`, `--world`, `--seed`, `--steam`, ...)
- [ ] Choose JSON lib (recommend nlohmann/json or glaze) and add via FetchContent
- [ ] Build config: warnings level, sanitizers preset (ASan/UBSan) for debug, `RelWithDebInfo` default, LTO release preset
- [ ] CMake presets for Linux/Windows; separate targets: `terraria_3d` (client+host), `terraria_3d_server` (headless), `terraria_3d_tests`
- [ ] **DECIDE** unity vs normal build; precompiled headers for GLM/Daxa (recommend PCH)

### 1.2 Platform / input
- [ ] Input layer: key/mouse/scroll/text -> per-frame snapshot (`platform/input.hpp`)
- [ ] Action bindings (rebindable, saved in settings), `PlayerInput` sampling
- [ ] Mouse capture for first-person look, raw mouse motion, sensitivity
- [ ] Gamepad support (GLFW gamepad API): sticks to move/look, triggers for mine/place, radial hotbar - **needed for Steam Deck**
- [ ] Window: fullscreen toggle, resize/minimize handling, DPI scale, window icon
- [ ] Wayland + X11 verified (already handled in `Window::native_info`)

### 1.3 Testing infrastructure
- [ ] Add unit test framework (Catch2 or doctest) + `terraria_3d_tests` target
- [ ] Deterministic tests for: coordinate math, palette chunk storage, worldgen determinism (same seed -> same hash), AABB collision, voxel raycast, serialization round-trips
- [ ] CI (GitHub Actions): Linux + Windows build, run tests, shader compile check
- [ ] **YOU** push the repo to GitHub (nothing is committed yet; first commit is still pending)

---

## Phase 2 - World data (CPU side)

### 2.1 Block / chunk data model
- [ ] `BlockRegistry` loaded from `data/blocks/*.json` (name -> id, def fields listed in `world/block.hpp`), air = id 0
- [ ] **DECIDE** voxel format on GPU: recommended **u16 block id per voxel** in chunk storage on CPU (palette-compressed) and a **flat u16 (or u32 with state/light) array per resident chunk on GPU**
- [ ] `Chunk`: palette compression (uniform / 1-16 bit), per-voxel state, version counter, dirty flags, `snapshot()`
- [ ] Block states (orientation, liquid level, growth) packed with the id for GPU (e.g. `u32 = id:16 | state:8 | light:8`)
- [ ] `World`: chunk map, `get_block/set_block`, bounds, dirty propagation to neighbours, `on_block_changed` hooks
- [ ] Chunk status machine: Empty -> Generating -> Generated -> Lighting -> Ready; ticket system for load/unload
- [ ] Test world size (e.g. 1024 x 512 x 1024) now; design chunk map, SVO page table and streaming so the world can become infinite (no world-sized arrays)

### 2.2 Persistence
- [ ] Chunk (de)serialization (palette + RLE, then zstd/lz4)
- [ ] Region files (8x8 columns), atomic writes, async saving
- [ ] `level.dat` (WorldInfo: seed, size, time, spawn, flags, block name table, version)
- [ ] Block-id remapping via name table on load
- [ ] Player data files (inventory, position, stats)
- [ ] Autosave scheduler, world lock file, `.bak` of `level.dat`
- [ ] Save format version + migration scaffold

### 2.3 Physics (shared by server and client prediction - must be deterministic)
- [ ] `AABB` vs voxel collision, axis-by-axis sweep
- [ ] Ground / step-up / sneak edge protection, fall damage distance
- [ ] Liquids: swim state, buoyancy; ladders/climb
- [ ] Voxel raycast (DDA) for mining/placing/outline - CPU version (GPU ray march is separate)
- [ ] Entity spatial hash
- [ ] Tests for all of the above

---

## Phase 3 - Rendering by voxel ray marching (compute)

> This replaces the mesher + chunk-mesh renderer in the skeleton. There is no per-chunk geometry. The GPU holds
> voxel data, and a compute shader traces one ray per pixel through it.

### 3.1 GPU voxel storage design (do the design task before writing code)
- [ ] **Write a short design note** (`docs/voxel_rendering.md`): data layout, memory budget, update path, traversal algorithm, LOD strategy
- [x] ~~DECIDE acceleration structure~~ -> **chunked SVO** (decided). Original options for reference:
  - **A (recommended to start): two-level grid** - a world-sized *page table* (one u32 per chunk -> index into a pool of resident chunks, or empty/uniform), each chunk 32^3 voxels, plus a per-chunk **8^3 brick occupancy bitmask** (64 bits per brick) used to skip empty space. Traverse with hierarchical DDA (world grid -> chunk -> brick -> voxel).
  - B: sparse voxel octree / 64-tree (better skipping and compression, costlier edits and more complex shader)
  - C: A now, move to B if empty-space skipping becomes the bottleneck
- [ ] Memory budget math for target GPUs (Steam Deck: shared 16 GB, ~1 GB VRAM carve-out by default -> budget voxel pool + framebuffers, set render distance accordingly)
- [ ] Chunk pool: fixed-size slots in one big buffer (or a few), free-list allocator, page-table indirection
- [ ] Uniform chunk optimization (all air / all stone) -> no pool slot, encoded in page table
- [ ] Voxel pool format: u16 ids + separate light buffer, or packed u32 - decide after profiling

### 3.2 Ray marching renderer
- [ ] Camera: view/proj matrices, ray generation from pixel (camera-relative coordinates, double -> float split for far-from-origin precision)
- [ ] Compute pass v0: one thread per pixel, DDA through the grid, output flat color per block id -> storage image -> blit to swapchain
- [ ] Output via storage image + `TRANSFER`/blit to swapchain (swapchain usage flags: `STORAGE` or copy)
- [ ] Texture atlas / texture array lookup in the compute shader (per-face UV from hit normal + hit position), mipmaps (use `textureGrad` with ray differentials or a per-pixel LOD estimate) and nearest-neighbour look
- [ ] Hierarchical traversal: skip empty bricks and empty chunks; tune workgroup size (8x8), early-out on max distance
- [ ] Surface normals/face id from DDA step axis (free)
- [ ] Ambient occlusion: per-voxel-corner AO computed in shader from neighbours at hit, or baked into light buffer
- [ ] Sun lighting: shadow ray per pixel (cheap with hierarchical DDA) toward the sun, soft day/night
- [ ] Block/sky light from the lighting engine (sampled from light buffer), smooth interpolation
- [ ] Sky: gradient, sun, moon, stars, clouds (computed for rays that miss), fog by distance
- [ ] Transparent blocks: water/glass/leaves - **DECIDE** approach: continue marching after hit with accumulated tint (recommended: handle in the same compute pass, cap at N transparent hits)
- [ ] Alpha-tested blocks (leaves, plants): treat per-voxel texel alpha during traversal
- [ ] Non-cube shapes (slabs, stairs, torches, fences): sub-voxel shape tests in shader from a small shape table (v2)
- [ ] Depth output (write linear depth to a buffer/image) so entities/particles/UI can composite
- [ ] Entities and items as **object-space voxel models** (each with its own small voxel grid + transform): ray march against the world SVO and against entity grids in the same pass (or a second pass with depth merge); design BVH/grid of entity instances for per-ray culling. Rotation = transform the ray into object space
- [ ] Block selection outline + mining crack overlay (analytic in shader from selected voxel)
- [ ] Underwater tint / fog when the eye is inside liquid

### 3.3 Streaming voxel data to the GPU
- [ ] Staging ring buffer (`DAXA_ENABLE_UTILS_MEM`), per-frame upload budget (bytes) to avoid hitches
- [ ] Chunk upload path: new chunk -> allocate pool slot -> copy -> update page table + brick masks
- [ ] Block edit path: small updates (batch of voxel writes) via a compute "apply edits" pass instead of re-uploading whole chunks
- [ ] Chunk eviction when far / unloaded; page-table update
- [ ] Brick-mask generation: CPU on upload (cheap) or GPU compute pass
- [ ] GPU time and VRAM stats shown in debug overlay

### 3.4 Performance and scalability
- [ ] GPU timestamps per pass (Daxa) and a profiling overlay
- [ ] **Resolution scaling**: render at lower internal resolution + upscale (bilinear -> FSR1-like) - **essential for Steam Deck**
- [ ] Settings: render distance, internal resolution scale, shadow quality, AO on/off, max transparent steps
- [ ] LOD for far terrain: coarser traversal levels (mip of occupancy / averaged color per 8^3 or 32^3) beyond N chunks - **DECIDE** v1 or later (recommend later, start with hard render distance + fog)
- [ ] Temporal accumulation / reprojection to cut ray cost (later; careful with ghosting on edits)
- [ ] Beam optimization: trace coarse 8x8 tile first to find a conservative start distance (later)
- [ ] Benchmark scene + target frame times: 60 fps @ 1080p on a mid desktop GPU, 40-60 fps @ 800p internal on Steam Deck - **YOU**: provide hardware numbers once you have a Deck / lower-end GPU to test

### 3.5 Render graph
- [ ] Enable `DAXA_ENABLE_UTILS_TASK_GRAPH` and `DAXA_ENABLE_UTILS_MEM` in CMake
- [ ] Task graph: upload edits -> ray march -> entity raster pass -> post (fog/tonemap/upscale) -> UI (ImGui + HUD) -> present
- [ ] Shader hot reload via PipelineManager (works already)
- [ ] `shaders/shared.inl`: GlobalUniforms, page table layout, push constants
- [ ] **Rewrite skeleton docs**: delete `render/chunk_mesher.hpp` and `render/chunk_renderer.hpp`, replace with `render/voxel_renderer.hpp` / `voxel_storage.hpp` matching the chosen design

---

## Phase 4 - Gameplay core (single player, local)

- [ ] Player entity: movement (walk, sprint, sneak, jump, swim), look, collision
- [ ] Block selection: raycast + outline
- [ ] Mining: progress by tool/hardness, crack stage, drops
- [ ] Placing: validation (not inside entities, support rules), hotbar selection
- [ ] World time and day/night driving sun direction + sky
- [ ] Minimal HUD: crosshair, hotbar, debug text
- [ ] Debug flycam / creative toggle
- [ ] Item pickup, simple dropped item entities
- [ ] Block updates: scheduled ticks + random ticks (bounded per tick)
- [ ] Falling blocks (sand/gravel)

---

## Phase 5 - World generation

- [ ] Noise library header (Perlin/OpenSimplex, FBM, domain warp) with tests
- [ ] Deterministic generation as pure function of (seed, chunk pos)
- [ ] Biome map (2D), blending, layer bands (space / sky / surface / underground / caverns / underworld)
- [ ] Heightmap terrain + base fill + bedrock/underworld floor
- [ ] 3D caves (noise caves + worms), cavern layer, surface entrances
- [ ] Ores by depth band with vein shapes (progression-gated)
- [ ] Liquid lakes, seas, lava; initial settling
- [ ] Trees, plants, cacti per biome (cross-chunk decoration)
- [ ] Structures: houses, dungeons, floating sky islands, loot chests (jittered grid, deterministic)
- [ ] Spawn point finder
- [ ] Worldgen runs on job threads; benchmark chunks/sec
- [ ] Worldgen preview/inspection tools in the dev editor (see Phase 11)
- [ ] World creation UI inputs: seed, size, difficulty

---

## Phase 6 - Lighting and fluids

- [ ] Light engine: sky + block light BFS add/remove, cross-chunk propagation, worker-thread jobs
- [ ] Light data layout on GPU (see 3.1) and sampling in the ray marcher
- [ ] Incremental updates on block edit; day/night as shader multiplier (no recompute)
- [ ] **DECIDE** client-side light computation vs server-sent light (recommend client recompute from chunk data, deterministic)
- [ ] Fluid simulation: scheduled ticks, down-then-sideways flow, levels, water+lava interaction
- [ ] Liquid rendering in the ray marcher (surface height from level, animated)
- [ ] Gameplay: swimming, drowning, lava damage, buckets

---

## Phase 7 - Networking (Steam + ENet)

> Existing: `NetServer` / `NetClient` / `NetworkManager` over ENet, typed packets, tests.

### 7.1 Transport abstraction
- [x] Backend interface (`transport_backend.hpp`); ENet and Steam implementations behind `NetServer`/`NetClient`
- [x] **Loopback transport** (in-process, thread-safe) for singleplayer / integrated server thread - no sockets
- [x] Simulated latency/loss/jitter wrapper for local testing of prediction (`SessionOptions::simulation`, debug UI sliders)
- [x] `NetworkManager` uses `Endpoint` / `ServerConfig` (transport chosen at runtime: ENet / Steam)

### 7.2 Steam integration
- [ ] **YOU**: create a Steamworks partner account and app ($100 fee) to get a real App ID. Until then use test App ID **480 (Spacewar)** for development
- [ ] **YOU**: install the Steam client on every dev/test machine and be logged in; for Steam Deck testing add the game as a non-Steam game or use the dev build
- [x] Library: Steamworks SDK `ISteamNetworkingSockets` (relay, SteamID identity)
- [x] Steamworks SDK placed in `./sdk` (git-ignored)
- [x] CMake: `TERRARIA_ENABLE_STEAM` (auto-on when `sdk/` exists), links `steam_api`, copies the runtime library next to the exe, writes `steam_appid.txt` (dev). Windows paths written but **untested**
- [x] `SteamPlatform` wrapper: init, callbacks each frame, shutdown, graceful failure when Steam is not running (app falls back to ENet)
- [x] Steam backend: `CreateListenSocketP2P` + loopback listener for the host's own client, `ConnectP2P` by SteamID, reliable/unreliable send flags, separate lane for bulk
- [x] Relay network access started at init (`InitRelayNetworkAccess`)
- [ ] **YOU + me**: test a real relay connection between two different Steam accounts on two machines (cannot be automated; same-account loopback is covered by `net_loopback_test`)
- [x] Friends flow: rich presence `connect` string, overlay "Join Game" callback, `+connect_steam <id>` launch argument -> `take_join_request()`
- [ ] Friends list UI in the main menu: list online friends running the game, invite button (`InviteUserToGame`)
- [ ] Steam lobbies (`ISteamMatchmaking`) for browsing public servers - decided yes, after friend-join works end to end
- [x] Identity: `NetServer::identity(peer)` = Steam-verified SteamID64 (never from packet fields)
- [ ] Optional: app-level auth ticket (`GetAuthSessionTicket`/`BeginAuthSession`) for ownership/VAC checks on public servers
- [ ] Player names/avatars from Steam, ban list by SteamID
- [ ] Steam Deck: verify overlay, controller glyphs, and suspend/resume handling (connection loss -> reconnect)
- [x] ENet path kept for: dedicated servers, LAN without Steam, tests
- [ ] Handshake: add content hash + build info (SteamID comes from the transport, not the packet)

### 7.3 Protocol and replication (see `net/*.hpp` design notes)
- [x] Handshake: version + content hash + name policy + bans + duplicate login + capacity, reject reasons (`ServerSession`)
- [ ] World info + block-name table packet after accept (needs world/registries)
- [x] Client connection state machine: Connecting -> Handshaking -> Active (`ClientSession`)
- [ ] Game-level LoadingWorld state (waiting for spawn chunks) on top of it
- [ ] Chunk streaming: per-player queue ordered by distance/view, bytes/tick budget, compressed payload, resend
- [ ] Block changes: single + batched per chunk per tick; ack/rejection with correction
- [ ] Player input stream (unreliable), server-side input queue with sequence numbers
- [ ] **Client-side prediction + server reconciliation** using the shared physics code
- [ ] Entity replication: relevance by distance, spawn/despawn, snapshots, interpolation buffer
- [x] Bit-packed serialization (`BitWriter`/`BitReader`, quantisation) + `EntitySnapshotBatch`, `MultiBlockChange`
- [ ] Bit-pack `PlayerInput` and add delta compression for snapshots once entities exist
- [x] LZ4 compression helper for bulk payloads
- [x] Per-peer rate limiting (token buckets) + kick on sustained flooding
- [ ] Gameplay validation of every server-bound packet (reach distance, cooldowns, bounds) - when those systems exist
- [ ] Bandwidth budget per client; priority accumulator
- [x] Kick / ban list (by transport identity) in `ServerSession`; [ ] chat + commands + permissions (game layer), [ ] ban list persistence
- [x] Disconnect / timeout / kick / reject reasons reach the player (`ClientSession::disconnect_reason`); [ ] show them in the UI; [ ] reconnect after Steam Deck suspend
- [x] Network debug window written (state, ping, traffic, simulation sliders); [ ] hook into the ImGui overlay once the ImGui renderer exists; [ ] prediction error display
- [ ] **Milestone 1 test**: two machines (Steam relay) and two local instances (loopback) mine/place blocks seen by both
- [x] Network layer regression test: `net_loopback_test` (ENet + Steam loopback)
- [ ] Add a fuzz/stress test for the packet parser and a flood test for the inbox cap

### 7.4 Server architecture
- [ ] `Server` class: fixed-tick loop, world step, player table, join/leave, autosave
- [ ] Integrated server on its own thread (own `NetServer`/transport), client talks via loopback or Steam
- [ ] Dedicated server executable (headless, no Vulkan/GLFW dependency), console commands, graceful SIGINT save
- [ ] Overload shedding and tick-time metrics

---

## Phase 8 - Survival systems (post milestone 1)

### 8.1 Items and inventory
- [ ] `ItemRegistry` from `data/items/*.json`; categories (block/tool/weapon/armor/accessory/consumable/material)
- [ ] `ItemStack`, `Container`; server-authoritative inventory actions with revision numbers
- [ ] Player inventory, hotbar, armor/accessory slots, trash slot
- [ ] Chests (block entities), shared access, open/close tracking
- [ ] Item prefixes/modifiers (Terraria-style) - later
- [ ] Inventory UI (drag/drop, shift-click, split), tooltips
- [ ] Gamepad-friendly inventory navigation (Steam Deck)

### 8.2 Crafting
- [ ] Recipes from `data/recipes/*.json`, tag ingredients (any_wood...)
- [ ] Station detection (nearby workbench/furnace/anvil)
- [ ] Craft request validation on server; furnace/smelting block entity (fuel, progress)
- [ ] Crafting UI with "craftable now" filtering

### 8.3 Combat and health
- [ ] Health, damage events, defense, invulnerability frames, knockback, death and respawn
- [ ] Melee hitbox arcs, ranged projectiles with gravity, magic, mana
- [ ] Armor sets and bonuses; accessories (double jump, speed...)
- [ ] PvP flag / teams
- [ ] Explosions (block destruction + damage)

### 8.4 Entities, mobs, NPCs
- [ ] Choose ECS (EnTT via FetchContent) and define components (see `entity/entity.hpp`)
- [ ] Mob registry from `data/mobs/*.json`
- [ ] AI basics: wander, chase, melee, ranged, flee, jump obstacles
- [ ] Spawning/despawning system per player (biome, depth, time, light level, caps)
- [ ] 3D pathfinding (A* with node budget) only where needed
- [ ] Flying / swimming / burrowing mobs
- [ ] Loot tables (`data/loot/`)
- [ ] Town NPCs: housing validity (flood-fill room check), arrival conditions, shops, happiness
- [ ] Entity rendering as object-space voxel models: part-based animation (rigid voxel parts with transforms), interpolation of remote players
- [ ] Player model/arms (first-person view model + third-person body seen by others)

### 8.5 Progression and events
- [ ] Progression flags (data-driven), unlocking ores/recipes/NPCs/spawns
- [ ] Bosses: arena rules, phases, summon items, boss bar
- [ ] Events: blood moon, invasions (waves, progress bar), weather
- [ ] Biome spreading (corruption/hallow-style) via random ticks, cleansing mechanic
- [ ] Game modes: survival, creative, spectator; difficulty
- [ ] Command registry (/tp, /give, /time, /gamemode ...) with permission levels

---

## Phase 9 - UI / UX

- [ ] Screen stack + base Screen, input focus rules (UI vs world)
- [ ] Main menu: singleplayer, host, join (Steam friends / lobby browser / direct IP for ENet), settings, quit
- [ ] World select + world creation (name, seed, size, difficulty)
- [ ] Pause menu, settings (video/audio/controls with rebinding), loading screen with progress
- [ ] HUD: health/mana, hotbar, buffs, boss bar, minimap, damage numbers, chat feed
- [ ] Death screen, map screen
- [ ] Localisation tables (`data/lang/en.json`) from the start
- [ ] Font rendering + icon rendering from the texture array
- [ ] UI scale, DPI awareness, safe layout for 1280x800 (Steam Deck)
- [ ] Controller navigation in all menus (focus, glyphs)
- [ ] Decide UI approach: ImGui styled (fast) vs custom textured-quad UI (recommended custom for the game UI, ImGui for debug only)

---

## Phase 10 - Audio

- [ ] Choose library (miniaudio recommended) and add via FetchContent
- [ ] SFX: steps by material, mining/placing/breaking, combat, mobs, UI
- [ ] Music: biome/depth/time/boss-driven with crossfades
- [ ] 3D positional audio, underground occlusion (voxel raycast), cave reverb preset
- [ ] Volume groups from settings
- [ ] **YOU**: source or commission sound and music assets (license-clean)

---

## Phase 11 - Dev tools / in-game editor

- [ ] ImGui debug overlay: FPS, frame graph, GPU pass timings, position, chunk counts, VRAM, network stats
- [ ] Free-cam / noclip, time-of-day slider, weather toggle
- [ ] Block picker / brush tools (place/replace/fill sphere/box), undo/redo (local, dev only)
- [ ] Chunk debug views: chunk borders, brick occupancy heatmap, ray step-count heatmap (very useful for ray-march perf), light levels, biome colors
- [ ] World inspector: worldgen parameters live-tweaking, regenerate chunk, structure placer
- [ ] Entity inspector / spawner, item giver
- [ ] Hot reload for shaders (exists) and JSON data (add file watcher)
- [ ] Console with commands (shared with in-game commands)
- [ ] Replay/record of inputs for reproducing bugs (stretch)
- [ ] Tracy (or similar) profiler integration, optional

---

## Phase 12 - Content pipeline and assets

- [ ] Texture pipeline: `assets/textures/blocks/*.png` -> texture array, animated tile support, mipmaps
- [ ] **YOU**: provide or create block textures (16x16 or 32x32), item icons, UI art, fonts. Decide art style (pixel-art, low-poly). Placeholder generator for dev so work is not blocked
- [ ] Voxel model format (custom): player, mobs, item drops, with palette + animation (bone/part transforms); editor/exporter tool - **David**
- [ ] JSON schemas + validation with clear error messages (path + field)
- [ ] Content hash computed from data files (used in handshake so clients/servers match)
- [ ] Resource pack override support (later)
- [ ] Structure format (`data/structures/`) and a tool to export structures from the editor
- [ ] **DECIDE** license/credits for third-party assets

---

## Phase 13 - Modding / extensibility (later, keep APIs ready)

- [ ] Keep all gameplay content in data and registries (already planned) so a mod is "extra data dir"
- [ ] Mod loader: load order, name-spaced ids (`modname:block`), content hash includes mods
- [ ] Optional scripting (Lua via sol2) for events/AI - only after core is stable

---

## Phase 14 - Platform support: Windows and Steam Deck

- [ ] Windows build in CI (MSVC or clang-cl), fix portability (paths, `std::format`, ENet winsock libs already in CMake)
- [ ] Windows: `steam_api64.dll` handling, installer/zip packaging
- [ ] Steam Deck: runs natively on Linux (SteamOS): test with Vulkan on RADV, verify Daxa required features (descriptor indexing, buffer device address, timeline semaphores, 64-bit atomics if used)
- [ ] Steam Deck defaults profile: internal resolution scale, render distance, 800p UI scale, 40/60 fps limiter
- [ ] Controller defaults and Steam Input action sets (menus vs gameplay) - decide Steam Input API vs GLFW gamepad (recommended: Steam Input for glyphs and remapping)
- [ ] Battery-aware options (frame cap), suspend/resume network recovery
- [ ] **YOU**: get a Steam Deck (or borrow one) for testing - there is no reliable way to simulate its GPU
- [ ] **YOU**: test on Windows machine (NVIDIA and/or AMD) and report driver issues
- [ ] Verify Daxa device selection picks the right GPU on laptops with iGPU + dGPU
- [ ] Shader compile on target platforms (glslang from source works; consider precompiled SPIR-V cache to cut startup time)

---

## Phase 15 - Hardening, release prep

- [ ] Crash reporting (minidump / backtrace) and logs location
- [ ] Anti-abuse: server-side validation audit, fuzz the packet parser (libFuzzer on `PacketInbox`/deserializers)
- [ ] Save corruption recovery, world backups
- [ ] Performance passes: CPU (worldgen, lighting, saving), GPU (ray march), memory
- [ ] Soak tests: 4+ players for hours; chunk load/unload churn; memory leak checks
- [ ] Steam page, achievements (Steamworks), cloud saves (optional), depots/build upload with `steamcmd`
- [ ] **YOU**: store page assets, pricing, legal (privacy policy, EULA), age rating questionnaire
- [ ] Versioning and update/compatibility policy for worlds and protocol

---

## Milestone map

| Milestone | Contents | Phases |
|---|---|---|
| **M0** (done) | Build, window, skeleton, ENet layer | 0 |
| **M1a** | Camera + ray-marched flat test world on screen, free-fly | 1, 2.1, 3.1-3.2 (v0) |
| **M1b** | Terrain gen, player physics, mine/place with GPU edit path | 2, 3.3, 4, 5 (basic) |
| **M1c** | **2 players** via loopback + ENet LAN, then Steam relay; prediction + block sync | 7 |
| **M2** | Lighting, fluids, inventory, crafting, items, HUD, menus | 6, 8.1-8.2, 9 |
| **M3** | Mobs, combat, NPCs, bosses, events, audio | 8.3-8.5, 10 |
| **M4** | Dev tools polish, Steam Deck + Windows release candidates | 11, 14, 15 |

---

## Open questions (answer these when you can; defaults are recommended in brackets)

Answered: (1) chunked SVO; (2) transparent blocks continue marching with capped steps; (3) entities are object-space voxels;
(4) hard render distance + fog first, far LOD later; (5) client-host over Steam sockets; (6) Steam lobbies; (7) fixed test world now, infinite later;
(8) no wall layer; (9) art + custom asset format by David; (10) Steam page/App ID much later.

Still open:
1. **SVO details**: branching factor (8-ary octree vs 64-tree/4x4x4), per-chunk SVO in a shared node pool vs one global tree, how edits update nodes on the GPU
2. **Infinite world**: chunk-key hashing vs hierarchical page table on the GPU; how far the SVO root extends
3. **Entity voxel models**: max resolution per model, shared palette, how many instances per ray (BVH vs uniform grid)

---

## Things only you can do (summary)

- [ ] Make the first git commit and set up a GitHub remote
- [ ] Steamworks partner account + App ID (use 480 until then); download Steamworks SDK into `third_party/steamworks/` (not committed)
- [ ] Hardware for testing: Steam Deck, a Windows PC, a lower-end GPU
- [ ] Art, sound and music assets or decisions on how to get them
- [ ] Answer the open questions above
- [ ] Playtesting with friends over Steam once Milestone 1c is reached
