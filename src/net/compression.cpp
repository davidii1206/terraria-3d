#include "net/compression.hpp"

#include <lz4.h>

#include <cstring>
#include <limits>

namespace t3d::net
{
    auto compress(std::span<std::byte const> raw) -> std::vector<std::byte>
    {
        if (raw.size() > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) { return {}; }
        auto const bound = static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(raw.size())));
        std::vector<std::byte> out(sizeof(std::uint32_t) + bound);
        auto const size32 = static_cast<std::uint32_t>(raw.size());
        std::memcpy(out.data(), &size32, sizeof(size32));
        int const written = LZ4_compress_default(reinterpret_cast<char const *>(raw.data()),
                                                 reinterpret_cast<char *>(out.data() + sizeof(size32)),
                                                 static_cast<int>(raw.size()), static_cast<int>(bound));
        if (written <= 0 && !raw.empty()) { return {}; }
        out.resize(sizeof(size32) + static_cast<std::size_t>(std::max(written, 0)));
        return out;
    }

    auto decompress(std::span<std::byte const> packed, std::size_t max_size) -> std::optional<std::vector<std::byte>>
    {
        std::uint32_t size32 = 0;
        if (packed.size() < sizeof(size32)) { return std::nullopt; }
        std::memcpy(&size32, packed.data(), sizeof(size32));
        if (size32 > max_size || size32 > static_cast<std::uint32_t>(LZ4_MAX_INPUT_SIZE)) { return std::nullopt; }

        std::vector<std::byte> out(size32);
        auto const body = packed.subspan(sizeof(size32));
        if (size32 == 0) { return body.size() <= 1 ? std::optional{std::move(out)} : std::nullopt; } // LZ4 encodes empty input as one token
        int const got = LZ4_decompress_safe(reinterpret_cast<char const *>(body.data()), reinterpret_cast<char *>(out.data()),
                                            static_cast<int>(body.size()), static_cast<int>(size32));
        if (got < 0 || static_cast<std::uint32_t>(got) != size32) { return std::nullopt; }
        return out;
    }
} // namespace t3d::net
