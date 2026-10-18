#pragma once
// Wire protocol: packet ids, serialization, versioning.
//
// Serialization: a small BitWriter/BitReader (or use a library such as bitsery / zpp_bits) with
// explicit field order; quantize floats (positions to 1/256 block, angles to 8-16 bits), varints for ids.
// Protocol version + game content hash exchanged in the handshake; mismatch -> refuse with a clear message.
//
// CLIENT -> SERVER
//   Handshake{protocol_version, username, content_hash, auth token}
//   PlayerInput{sequence, input state, yaw/pitch}            (every client tick, unreliable)
//   BlockDig{pos, action: start/stop/finish}  BlockPlace{pos, face, hotbar slot, hit point}
//   UseItem / Attack{target entity or block}
//   InventoryAction{container, slot(s), op, revision}  CraftRequest{recipe, count}
//   ChatMessage / Command   Respawn   Ping   ChunkAck / ChunkRequestResend
// SERVER -> CLIENT
//   HandshakeAccept{player id, world info, spawn, server tick rate, block name table} | Reject{reason}
//   ChunkData{pos, compressed palette data}  ChunkUnload{pos}
//   BlockChange{pos, new id, state}  MultiBlockChange{chunk, list}   (batched per tick)
//   EntitySpawn / EntityDespawn / EntitySnapshot{tick, [entity id, delta-compressed state]}
//   PlayerStateCorrection{last processed input seq, authoritative position/velocity}
//   InventoryUpdate{container, revision, changed slots}  Health/Hunger/Buff updates
//   TimeUpdate{world time, weather}  Chat/SystemMessage  Sound/Particle/Effect events  Kick{reason}
//
// Efficiency rules: send only what changed (dirty masks), only to players that can see it (relevance
// by distance, see replication.hpp), batch block changes per chunk per tick, compress chunk payloads (zstd/lz4).
// Security: validate every length/index read from the wire, cap packet sizes and per-second rates.

#include "core/types.hpp"

namespace t3d
{
    enum class PacketId : u8
    {
        Handshake, HandshakeAccept, HandshakeReject,
        PlayerInput, PlayerStateCorrection,
        ChunkData, ChunkUnload, BlockChange, MultiBlockChange, BlockDig, BlockPlace,
        EntitySpawn, EntityDespawn, EntitySnapshot,
        InventoryAction, InventoryUpdate, CraftRequest,
        Chat, TimeUpdate, Kick, Ping, Pong,
        Count
    };

    // TODO: packet structs + serialize()/deserialize() for each.
} // namespace t3d
