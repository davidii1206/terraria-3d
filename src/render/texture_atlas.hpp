#pragma once
// Block/item textures.
//
// Use a 2D texture ARRAY (one layer per 16x16 or 32x32 tile) rather than a packed atlas:
// no bleeding between tiles, and mipmaps + anisotropic filtering work per tile.
//  - Built at startup from assets/textures/blocks/*.png (name = file name), layer index stored
//    in the block registry (world/block.hpp) per face (top/bottom/side).
//  - Animated tiles (water, lava, fire, torches): extra frames as consecutive layers + frame
//    time; the shader picks the layer from global time.
//  - Resource packs: later, load a second directory that overrides tiles by name.
//  - Item icons for the inventory UI: render from the same array (flat) or pre-render isometric.

namespace t3d
{
    class TextureAtlas
    {
      public:
        // TODO: load(directory); layer_of(std::string_view name) -> u32; gpu image + sampler.
    };
} // namespace t3d
