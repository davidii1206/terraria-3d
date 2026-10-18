#pragma once
// Block definitions and the block registry.
//
// A block is a numeric BlockId (u16) in chunk storage; everything else lives in a BlockDef
// looked up from the registry, loaded at startup from data/blocks/*.json so content (and mods)
// needs no recompilation.
//
// BlockDef fields (planned):
//   name            "terraria:stone"              stable string id (used in saves/mods/network handshake)
//   textures        top / bottom / side layer names -> TextureAtlas layers
//   render_layer    OPAQUE | CUTOUT | TRANSPARENT | NONE (air) | MODEL (custom shape)
//   shape           full cube | slab | stair | torch | fence | custom box list (collision + render)
//   solid           blocks movement?                (water: false, torch: false)
//   hardness        mining time multiplier; INFINITE for bedrock
//   tool_tier       minimum pickaxe/axe/shovel tier needed (Terraria-style progression: copper -> ... )
//   tool_type       which tool is "correct"
//   drops           loot table (item, count range, chance) - can differ by tool
//   light_emission  0..15 (torch, lava, glowstone)
//   light_filter    how much it dims light passing through (leaves 1, water 2, stone 15)
//   liquid          LiquidType + flow rules (see world/fluids.hpp)
//   gravity         falls when unsupported (sand, gravel)
//   support rules   must sit on a solid block (torch, flowers, sapling); "wall" attach (torch on wall)
//   random_tick     grass spread/decay, crops, leaf decay, sapling growth
//   interactable    on_use hook: door, chest, workbench, furnace, sign, bed, lever
//   block_entity    does it have extra per-instance data? (chest inventory, furnace progress, sign text)
//   sounds          step / break / place
//   tags            "mineable_with_pickaxe", "ore", "flammable" ... used by gameplay rules
//
// Per-block extra state ("facing", "open", "age", "water level") is stored as a small
// 'state' value alongside the id (see chunk.hpp) so doors/stairs/crops don't need one id per variant.
//
// Terraria extras to plan for: background WALLS (a second layer behind the block, for player-built
// houses and caves), wiring/actuators (optional later), platforms, furniture with multi-block footprints.

#include "core/types.hpp"
#include <string>

namespace t3d
{
    struct BlockDef
    {
        std::string name;
        // TODO: fields listed above.
    };

    class BlockRegistry
    {
      public:
        // TODO: load_from_directory(path); BlockDef const& get(BlockId); BlockId find(std::string_view name);
        // Order of registration defines numeric ids; air must be id 0.
    };
} // namespace t3d
