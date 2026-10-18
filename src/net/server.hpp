#pragma once
// Authoritative game server. Runs in three configurations from the same code:
//   1. Dedicated: headless (no window/renderer), reads ServerSettings, listens on a UDP port.
//   2. Integrated (singleplayer / "host game"): lives inside the client process on its own thread,
//      reached through the loopback transport. Optionally opens a port to let friends join (LAN/hosted).
//   3. (Tests) Server with simulated clients for automated tests and load tests.
//
// Server loop (fixed SERVER_TICK_RATE, accumulate real time, never skip logic when behind - cap catch-up):
//   1. receive packets -> per-player input queues / action requests (validated, rate limited)
//   2. world step: scheduled block ticks, random ticks, fluids, lighting jobs results
//   3. entity systems (entity/entity.hpp order): inputs -> AI -> physics -> combat -> spawning
//   4. chunk lifecycle: load/generate around players, unload/save unused ones
//   5. replication (net/replication.hpp): send chunks, block changes, snapshots
//   6. housekeeping: autosave schedule, timeouts, console commands, metrics (tick time, players, memory)
// Overload handling: if a tick takes longer than the budget, log it and shed optional work (fewer mob AI updates, fluid steps)
// rather than letting the whole game slow down.
//
// Player join: handshake -> load/create player data -> send world info + initial chunks -> spawn entity ->
//   announce to others. Leave: save player, remove entity, free chunk tickets.
// Admin: ops list, whitelist, bans, kick, console + in-game /commands, graceful shutdown with save.

namespace t3d
{
    class Server
    {
      public:
        // TODO: start(ServerSettings), tick(), stop(); player table; command execution.
    };
} // namespace t3d
