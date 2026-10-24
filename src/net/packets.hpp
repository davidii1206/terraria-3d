#pragma once
// Wire protocol: packet ids and payload structs.
//
// WIRE FORMAT:  [u8 PacketType][payload bytes]
//   * The type byte is written by the transport from T::TYPE - it is NOT a struct member, so a packet
//     can never be sent with the wrong tag.
//   * Fixed-size packets: the payload is the raw struct bytes. A struct must be trivially copyable
//     (no pointers, std::string, vectors), use fixed-width types and fixed-size char arrays for text.
//     The receiver rejects any packet whose payload size != sizeof(T).
//   * Variable-size packets (chunk data, ...): a fixed header struct followed by raw bytes, sent with
//     NetServer::send_bytes and read with receive_raw; the header carries the byte count.
//   * Both ends must be built with the same compiler/ABI for now (struct padding and float format are
//     sent as-is). When cross-platform play or bandwidth matters, move hot packets (snapshots, inputs,
//     block changes) to a bit-packed writer/reader with quantised floats; cold packets can stay as structs.
//
// CHANNELS (see net/transport.hpp):  reliable-ordered (default) | unreliable-sequenced | bulk (chunks)
//
// SECURITY: everything arriving at the server is untrusted. Validate ranges/indices/lengths before use,
// rate-limit per peer, and never trust client-provided positions, damage or item counts.
//
// DIRECTION KEY:  C->S client to server, S->C server to client, S->C* broadcast.

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace t3d::net
{
    inline constexpr std::uint32_t PROTOCOL_VERSION = 1;

    enum class PacketType : std::uint8_t
    {
        Ping = 1,              // C->S
        Pong,                  // S->C (echoes Ping.timestamp)

        // --- Session ---
        Handshake,             // C->S  first packet after connecting
        HandshakeAccept,       // S->C  player id, entity id, spawn
        HandshakeReject,       // S->C  version mismatch / server full / banned
        PlayerJoined,          // S->C* another player entered the world
        PlayerLeft,            // S->C* another player left
        Kick,                  // S->C  server is closing this connection
        Chat,                  // C->S and S->C*

        // --- Simulation ---
        PlayerInput,           // C->S  every client tick (unreliable)
        PlayerStateCorrection, // S->C  authoritative state + last processed input sequence
        EntitySpawn,           // S->C
        EntityDespawn,         // S->C
        EntitySnapshot,        // S->C  batched transforms/velocities (unreliable, bit-packed, see messages.hpp)
        TimeUpdate,            // S->C  world clock

        // --- World ---
        ChunkData,             // S->C  header + compressed payload (variable size, bulk channel)
        ChunkUnload,           // S->C
        BlockChange,           // S->C* authoritative single block edit
        BlockDig,              // C->S  start/stop/finish mining
        BlockPlace,            // C->S
        MultiBlockChange,      // S->C* batched block edits for one chunk (bit-packed, see messages.hpp)

        Count // keep last: used to reject unknown type bytes

        // TODO: InventoryAction (C->S), InventoryUpdate (S->C), CraftRequest (C->S), MultiBlockChange,
        //       Health/Buff updates, Sound/Particle/Effect events, Weather, Damage events.
    };

    // Every payload struct declares `static constexpr PacketType TYPE`.
    template <typename T>
    concept WirePacket = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
                         requires {
                             { T::TYPE } -> std::convertible_to<PacketType>;
                         };

    // --- Utility --------------------------------------------------------------------------------

    struct PingPacket
    {
        static constexpr PacketType TYPE = PacketType::Ping;
        std::uint32_t timestamp_ms = 0; // sender clock, echoed back so the sender can compute RTT
    };

    struct PongPacket
    {
        static constexpr PacketType TYPE = PacketType::Pong;
        std::uint32_t timestamp_ms = 0;
    };

    // --- Session --------------------------------------------------------------------------------

    struct HandshakePacket
    {
        static constexpr PacketType TYPE = PacketType::Handshake;
        std::uint32_t protocol_version = PROTOCOL_VERSION;
        std::uint64_t content_hash = 0; // hash of the loaded data/ registries (blocks, items, ...); must match the server
        char username[32]{};            // the session layer NUL-terminates and sanitises it before anyone uses it
    };

    struct HandshakeAcceptPacket
    {
        static constexpr PacketType TYPE = PacketType::HandshakeAccept;
        std::uint16_t player_id = 0;     // filled in by ServerSession::accept
        std::uint32_t tick_rate = 30;    // filled in by ServerSession::accept
        char username[32]{};             // final (sanitised, de-duplicated) name; filled in by ServerSession::accept
        std::uint32_t entity_id = 0;
        float spawn_x = 0, spawn_y = 0, spawn_z = 0;
        // TODO: world info (seed, size, time), block-name -> id table (variable size, separate packet).
    };

    enum class RejectReason : std::uint8_t
    {
        Other, VersionMismatch, ContentMismatch, ServerFull, BadUsername, DuplicateLogin, Banned, Timeout, Malformed,
    };

    struct HandshakeRejectPacket
    {
        static constexpr PacketType TYPE = PacketType::HandshakeReject;
        RejectReason code = RejectReason::Other;
        char reason[64]{}; // human readable, shown to the player
    };

    struct PlayerJoinedPacket
    {
        static constexpr PacketType TYPE = PacketType::PlayerJoined;
        std::uint16_t player_id = 0;
        std::uint32_t entity_id = 0;
        char username[32]{};
        float x = 0, y = 0, z = 0;
    };

    struct PlayerLeftPacket
    {
        static constexpr PacketType TYPE = PacketType::PlayerLeft;
        std::uint16_t player_id = 0;
        std::uint32_t entity_id = 0;
    };

    struct KickPacket
    {
        static constexpr PacketType TYPE = PacketType::Kick;
        char reason[64]{};
    };

    struct ChatPacket
    {
        static constexpr PacketType TYPE = PacketType::Chat;
        std::uint16_t sender_id = 0; // filled in by the server; ignored when sent by a client
        char message[256]{};
    };

    // --- Simulation -----------------------------------------------------------------------------

    // Player intent only - never positions (see entity/player.hpp for prediction/reconciliation).
    struct PlayerInputPacket
    {
        static constexpr PacketType TYPE = PacketType::PlayerInput;
        enum Flags : std::uint8_t { Jump = 1, Sneak = 2, Sprint = 4, Attack = 8, Use = 16 };

        std::uint32_t sequence = 0; // monotonically increasing, echoed in PlayerStateCorrection
        float move_x = 0, move_z = 0; // strafe / forward, normalised
        float yaw = 0, pitch = 0;
        std::uint8_t flags = 0;
        std::uint8_t hotbar_slot = 0;
    };

    struct PlayerStateCorrectionPacket
    {
        static constexpr PacketType TYPE = PacketType::PlayerStateCorrection;
        std::uint32_t last_processed_input = 0;
        double x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
    };

    struct EntitySpawnPacket
    {
        static constexpr PacketType TYPE = PacketType::EntitySpawn;
        std::uint32_t entity_id = 0;
        std::uint16_t kind = 0; // index into the mob/item-drop/projectile registry
        float x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
    };

    struct EntityDespawnPacket
    {
        static constexpr PacketType TYPE = PacketType::EntityDespawn;
        std::uint32_t entity_id = 0;
    };

    struct TimeUpdatePacket
    {
        static constexpr PacketType TYPE = PacketType::TimeUpdate;
        std::uint64_t world_tick = 0; // time of day derives from this
        std::uint8_t weather = 0;
    };

    // --- World ----------------------------------------------------------------------------------

    // Followed by `payload_size` bytes of compressed chunk data (palette + packed indices, see world/chunk.hpp).
    struct ChunkDataHeader
    {
        static constexpr PacketType TYPE = PacketType::ChunkData;
        std::int32_t cx = 0, cy = 0, cz = 0;
        std::uint32_t payload_size = 0;
    };

    struct ChunkUnloadPacket
    {
        static constexpr PacketType TYPE = PacketType::ChunkUnload;
        std::int32_t cx = 0, cy = 0, cz = 0;
    };

    struct BlockChangePacket
    {
        static constexpr PacketType TYPE = PacketType::BlockChange;
        std::int32_t x = 0, y = 0, z = 0;
        std::uint16_t block_id = 0;
        std::uint8_t state = 0;
        // TODO: batch per chunk per tick (MultiBlockChange).
    };

    struct BlockDigPacket
    {
        static constexpr PacketType TYPE = PacketType::BlockDig;
        enum class Action : std::uint8_t { Start, Cancel, Finish };

        std::int32_t x = 0, y = 0, z = 0;
        Action action = Action::Start;
    };

    struct BlockPlacePacket
    {
        static constexpr PacketType TYPE = PacketType::BlockPlace;
        std::int32_t x = 0, y = 0, z = 0; // block that was clicked
        std::uint8_t face = 0;            // 0..5 = -X +X -Y +Y -Z +Z
        std::uint8_t hotbar_slot = 0;
    };
} // namespace t3d::net
