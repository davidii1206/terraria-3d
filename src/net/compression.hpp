#pragma once
// LZ4 block compression for bulk payloads (chunk data, world info tables, ...).
//
// Format: [u32 raw_size, little endian][lz4 block]. decompress() refuses payloads that claim more than
// `max_size` bytes (decompression-bomb protection) and returns nullopt on any corruption.
// Use for Channel::Bulk traffic; do NOT bother for tiny packets (inputs, snapshots) - they are already bit-packed.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace t3d::net
{
    [[nodiscard]] auto compress(std::span<std::byte const> raw) -> std::vector<std::byte>;
    [[nodiscard]] auto decompress(std::span<std::byte const> packed, std::size_t max_size) -> std::optional<std::vector<std::byte>>;
} // namespace t3d::net
