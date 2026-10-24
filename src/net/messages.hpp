#pragma once
// Bit-packed, variable-length messages. Same idea as WirePacket structs but encoded with BitWriter/BitReader:
// smaller on the wire, endian/ABI independent, and safe against hostile counts.
//
// A BitMessage has `static constexpr PacketType TYPE`, `void write(BitWriter&) const` and
// `bool read(BitReader&)` (return false / reader.fail() on invalid data). NetClient/NetServer send and receive
// them with the same send()/receive<T>() calls as plain packets.
//
// Quantisation ranges live here, in one place, so both ends always agree.

#include "net/packets.hpp"
#include "net/serialization.hpp"

#include <algorithm>
#include <concepts>
#include <vector>

namespace t3d::net
{
    template <typename T>
    concept BitMessage = requires(T const & c, T & m, BitWriter & w, BitReader & r) {
        { T::TYPE } -> std::convertible_to<PacketType>;
        { c.write(w) };
        { m.read(r) } -> std::convertible_to<bool>;
    };

    // Positions: signed 32-bit fixed point, 1/256 block (+-8M blocks - plenty for an "infinite" world as long
    // as the server keeps coordinates in range). Velocities: +-128 blocks/s in 12 bits. Angles: 10 / 9 bits.
    inline constexpr int POSITION_FRAC_BITS = 8;
    inline constexpr float MAX_SPEED = 128.0f;
    inline constexpr float PI = 3.14159265358979f;

    // One entity's state inside an EntitySnapshotBatch.
    struct EntityState
    {
        std::uint32_t entity_id = 0;
        double x = 0, y = 0, z = 0;
        float yaw = 0;    // [-pi, pi]
        float pitch = 0;  // [-pi/2, pi/2]
        float vx = 0, vy = 0, vz = 0;

        void write(BitWriter & w) const
        {
            w.write_varuint(entity_id);
            w.write_fixed32(x, POSITION_FRAC_BITS);
            w.write_fixed32(y, POSITION_FRAC_BITS);
            w.write_fixed32(z, POSITION_FRAC_BITS);
            w.write_quantized(yaw, -PI, PI, 10);
            w.write_quantized(pitch, -PI / 2, PI / 2, 9);
            w.write_quantized(vx, -MAX_SPEED, MAX_SPEED, 12);
            w.write_quantized(vy, -MAX_SPEED, MAX_SPEED, 12);
            w.write_quantized(vz, -MAX_SPEED, MAX_SPEED, 12);
        }
        void read(BitReader & r)
        {
            entity_id = static_cast<std::uint32_t>(r.read_varuint());
            x = r.read_fixed32(POSITION_FRAC_BITS);
            y = r.read_fixed32(POSITION_FRAC_BITS);
            z = r.read_fixed32(POSITION_FRAC_BITS);
            yaw = r.read_quantized(-PI, PI, 10);
            pitch = r.read_quantized(-PI / 2, PI / 2, 9);
            vx = r.read_quantized(-MAX_SPEED, MAX_SPEED, 12);
            vy = r.read_quantized(-MAX_SPEED, MAX_SPEED, 12);
            vz = r.read_quantized(-MAX_SPEED, MAX_SPEED, 12);
        }
    };

    // S->C, Channel::Unreliable. Many entities per packet (~16 bytes each).
    // TODO: delta-compress against the last snapshot the client acknowledged.
    struct EntitySnapshotBatch
    {
        static constexpr PacketType TYPE = PacketType::EntitySnapshot;
        static constexpr std::size_t MAX_ENTITIES = 1024;

        std::uint32_t tick = 0;
        std::vector<EntityState> entities;

        void write(BitWriter & w) const
        {
            w.write_varuint(tick);
            w.write_varuint(entities.size());
            for (auto const & e : entities) { e.write(w); }
        }
        auto read(BitReader & r) -> bool
        {
            tick = static_cast<std::uint32_t>(r.read_varuint());
            auto const count = r.read_count(MAX_ENTITIES);
            entities.clear();
            entities.reserve(std::min<std::size_t>(count, r.bits_left() / 8)); // never trust the count for allocation
            for (std::size_t i = 0; i < count && r.ok(); ++i)
            {
                EntityState e;
                e.read(r);
                entities.push_back(e);
            }
            return r.ok();
        }
    };

    // S->C*, Channel::Reliable. All block edits of one chunk during one server tick.
    struct MultiBlockChange
    {
        static constexpr PacketType TYPE = PacketType::MultiBlockChange;
        static constexpr std::size_t MAX_CHANGES = 32768;
        static constexpr int LOCAL_INDEX_BITS = 15; // 32^3 = 2^15 voxels per chunk

        struct Change
        {
            std::uint16_t local_index = 0; // x + z*32 + y*32*32
            std::uint16_t block_id = 0;
            std::uint8_t state = 0;
        };

        std::int32_t cx = 0, cy = 0, cz = 0;
        std::vector<Change> changes;

        void write(BitWriter & w) const
        {
            w.write_i32(cx);
            w.write_i32(cy);
            w.write_i32(cz);
            w.write_varuint(changes.size());
            for (auto const & c : changes)
            {
                w.write_bits(c.local_index, LOCAL_INDEX_BITS);
                w.write_u16(c.block_id);
                w.write_u8(c.state);
            }
        }
        auto read(BitReader & r) -> bool
        {
            cx = r.read_i32();
            cy = r.read_i32();
            cz = r.read_i32();
            auto const count = r.read_count(MAX_CHANGES);
            changes.clear();
            changes.reserve(std::min<std::size_t>(count, r.bits_left() / 8));
            for (std::size_t i = 0; i < count && r.ok(); ++i)
            {
                Change c;
                c.local_index = static_cast<std::uint16_t>(r.read_bits(LOCAL_INDEX_BITS));
                c.block_id = r.read_u16();
                c.state = r.read_u8();
                changes.push_back(c);
            }
            return r.ok();
        }
    };
} // namespace t3d::net
