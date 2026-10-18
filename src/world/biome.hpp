#pragma once
// Biomes and depth layers.
//
// Two orthogonal axes, like Terraria:
//  * HORIZONTAL biome (what's the surface like here): forest, desert, snow, jungle, ocean,
//    mushroom, corruption/crimson-style evil biome, hallow-style good biome (spreads over time!).
//  * VERTICAL layer (how deep are we): SPACE, SKY (floating islands), SURFACE, UNDERGROUND (dirt+stone),
//    CAVERNS (open caves, lava appears), UNDERWORLD (hell, ash, hellstone, boss area).
//
// A BiomeDef (data/biomes/*.json) defines: surface/subsurface blocks, tree & plant types, ambient
// colours (grass/water/fog tint, sky colour), music track, ambient sounds, mob spawn tables per
// time of day, weather (rain, snow, sandstorm), temperature (affects fluid freezing).
// Players "are in" a biome based on the counted blocks around them (Terraria's method)
// or simply the biome at their position - decide when implementing.
//
// Spreading biomes (corruption, hallow, grass, mycelium): driven by random block ticks
// (world/world.hpp) with a "purifier"/"cleansing" counter-mechanic.

namespace t3d
{
    enum class Layer { Space, Sky, Surface, Underground, Caverns, Underworld };

    struct BiomeDef
    {
        // TODO: fields listed above.
    };
} // namespace t3d
