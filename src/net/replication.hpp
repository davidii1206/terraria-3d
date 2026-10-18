#pragma once
// Server -> client state replication: who needs to know about what, and how often.
//
// Relevance: each player has a "view radius" (chunks) - chunks and entities inside it are replicated.
//   Use a spatial hash of entities by chunk to find them fast. Entities entering the radius -> EntitySpawn;
//   leaving -> EntityDespawn.
// Chunk streaming: queue per player ordered by distance (prefer the chunk the player stands in and
//   the ones they look toward), limited chunks/tick and bytes/tick; resend on request. After a block edit
//   in an already-sent chunk, send BlockChange instead of the whole chunk.
// Entity snapshots: every N ticks (e.g. 10-20 Hz), delta-compressed against the last state each client acknowledged;
//   quantised position/rotation; low-priority entities (far mobs) update less often.
// Block changes: collected per tick per chunk, sent as MultiBlockChange to subscribed players only.
// Events (sounds, particles, damage numbers) are one-shot reliable-ish messages, not state.
// Bandwidth budget per client with priority accumulator (closest/most-changed first).
// Client side: apply to its World cache + entity view store; interpolation buffer ~2 snapshots;
//   chunk arrival triggers meshing (render/chunk_mesher.hpp).

namespace t3d
{
    class Replicator
    {
      public:
        // TODO: void tick(ServerWorldState&, std::span<ClientConnection>);
    };
} // namespace t3d
