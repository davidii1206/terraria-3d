#pragma once
// Bit-level serialization for hot / variable-length packets (snapshots, block-change batches, ...).
//
// BitWriter appends to an internal buffer; BitReader reads from a span and NEVER reads out of bounds:
// after any read past the end (or any failed validation) ok() is false and all further reads return 0.
// Decoders must check ok() at the end and treat !ok() as a malformed packet.
//
// Floats are sent quantised: write_quantized(value, min, max, bits). Choose ranges per field and keep
// them in one place (messages.hpp) so both ends agree.
// Bit order is little-endian; the byte layout is identical on every platform.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace t3d::net
{
    class BitWriter
    {
      public:
        void write_bits(std::uint64_t value, int bits)
        {
            for (int written = 0; written < bits;)
            {
                if (bit_pos_ == 0) { bytes_.push_back(0); }
                int const take = std::min(bits - written, 8 - bit_pos_);
                auto const chunk = static_cast<std::uint8_t>((value >> written) & ((1u << take) - 1u));
                bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (chunk << bit_pos_));
                bit_pos_ = (bit_pos_ + take) % 8;
                written += take;
            }
        }
        void write_bool(bool v) { write_bits(v ? 1 : 0, 1); }
        void write_u8(std::uint8_t v) { write_bits(v, 8); }
        void write_u16(std::uint16_t v) { write_bits(v, 16); }
        void write_u32(std::uint32_t v) { write_bits(v, 32); }
        void write_u64(std::uint64_t v) { write_bits(v, 64); }
        void write_i32(std::int32_t v) { write_bits(static_cast<std::uint32_t>(v), 32); }
        void write_f32(float v) { write_bits(std::bit_cast<std::uint32_t>(v), 32); }

        // LEB128-style: 7 payload bits + continuation flag per byte. Small numbers cost one byte.
        void write_varuint(std::uint64_t v)
        {
            do
            {
                std::uint64_t const part = v & 0x7F;
                v >>= 7;
                write_bits(part | (v != 0 ? 0x80 : 0), 8);
            } while (v != 0);
        }

        // value is clamped to [min, max] and stored in `bits` bits (max error = (max-min) / 2^(bits+1)).
        void write_quantized(float value, float min, float max, int bits)
        {
            auto const levels = static_cast<float>((1ull << bits) - 1);
            float const t = std::clamp((value - min) / (max - min), 0.0f, 1.0f);
            write_bits(static_cast<std::uint64_t>(std::lround(t * levels)), bits);
        }

        // Fixed point with `frac_bits` fractional bits in a signed 32-bit field (positions: 8 frac bits = 1/256 block).
        void write_fixed32(double value, int frac_bits)
        {
            write_i32(static_cast<std::int32_t>(std::llround(value * static_cast<double>(1ll << frac_bits))));
        }

        void write_bytes(std::span<std::byte const> data)
        {
            for (std::byte b : data) { write_bits(static_cast<std::uint8_t>(b), 8); }
        }

        [[nodiscard]] auto bytes() const -> std::span<std::byte const>
        {
            return std::as_bytes(std::span{bytes_});
        }
        [[nodiscard]] auto size_bytes() const -> std::size_t { return bytes_.size(); }

      private:
        std::vector<std::uint8_t> bytes_;
        int bit_pos_ = 0; // bits already used in bytes_.back(); 0 = start a new byte
    };

    class BitReader
    {
      public:
        explicit BitReader(std::span<std::uint8_t const> data) : data_{data} {}

        [[nodiscard]] auto read_bits(int bits) -> std::uint64_t
        {
            if (failed_ || bit_cursor_ + static_cast<std::size_t>(bits) > data_.size() * 8)
            {
                failed_ = true;
                return 0;
            }
            std::uint64_t value = 0;
            for (int read = 0; read < bits;)
            {
                std::size_t const byte = bit_cursor_ / 8;
                int const offset = static_cast<int>(bit_cursor_ % 8);
                int const take = std::min(bits - read, 8 - offset);
                std::uint64_t const part = (static_cast<std::uint64_t>(data_[byte]) >> offset) & ((1u << take) - 1u);
                value |= part << read;
                read += take;
                bit_cursor_ += static_cast<std::size_t>(take);
            }
            return value;
        }
        [[nodiscard]] auto read_bool() -> bool { return read_bits(1) != 0; }
        [[nodiscard]] auto read_u8() -> std::uint8_t { return static_cast<std::uint8_t>(read_bits(8)); }
        [[nodiscard]] auto read_u16() -> std::uint16_t { return static_cast<std::uint16_t>(read_bits(16)); }
        [[nodiscard]] auto read_u32() -> std::uint32_t { return static_cast<std::uint32_t>(read_bits(32)); }
        [[nodiscard]] auto read_u64() -> std::uint64_t { return read_bits(64); }
        [[nodiscard]] auto read_i32() -> std::int32_t { return static_cast<std::int32_t>(read_bits(32)); }
        [[nodiscard]] auto read_f32() -> float { return std::bit_cast<float>(static_cast<std::uint32_t>(read_bits(32))); }

        [[nodiscard]] auto read_varuint() -> std::uint64_t
        {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                std::uint64_t const byte = read_bits(8);
                value |= (byte & 0x7F) << shift;
                if ((byte & 0x80) == 0 || failed_) { return failed_ ? 0 : value; }
            }
            failed_ = true; // over-long encoding
            return 0;
        }

        [[nodiscard]] auto read_quantized(float min, float max, int bits) -> float
        {
            auto const levels = static_cast<float>((1ull << bits) - 1);
            return min + (static_cast<float>(read_bits(bits)) / levels) * (max - min);
        }

        [[nodiscard]] auto read_fixed32(int frac_bits) -> double
        {
            return static_cast<double>(read_i32()) / static_cast<double>(1ll << frac_bits);
        }

        // Reads a count and validates it against `max` (protects against huge allocations from hostile packets).
        [[nodiscard]] auto read_count(std::size_t max) -> std::size_t
        {
            auto const n = read_varuint();
            if (n > max) { failed_ = true; return 0; }
            return static_cast<std::size_t>(n);
        }

        void fail() { failed_ = true; }
        [[nodiscard]] auto ok() const -> bool { return !failed_; }
        [[nodiscard]] auto bits_left() const -> std::size_t { return data_.size() * 8 - bit_cursor_; }

      private:
        std::span<std::uint8_t const> data_;
        std::size_t bit_cursor_ = 0;
        bool failed_ = false;
    };
} // namespace t3d::net
