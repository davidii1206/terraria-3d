# data/ - game content as data

Everything the game *contains* (as opposed to how it *works*) should live here as JSON so it can be
edited without recompiling and extended by mods later. Loaded at startup by the registries.

```
data/
  blocks/      one file per block or per group      -> src/world/block.hpp
  items/       tools, weapons, armor, materials     -> src/game/items.hpp
  recipes/     crafting recipes                      -> src/game/crafting.hpp
  mobs/        mobs, NPCs, bosses                    -> src/entity/mob.hpp
  biomes/      biome + layer definitions             -> src/world/biome.hpp
  loot/        loot tables (mob drops, chests)
  structures/  worldgen structures (houses, dungeons) as block palettes + layouts
  events/      invasions, weather, progression flags -> src/game/events.hpp
  lang/        en.json ... localisation strings      -> src/ui/ui.hpp
```

Textures, models and sounds live in `assets/` (`assets/textures/blocks`, `assets/models`, `assets/sounds`, `assets/music`).
Shaders live in `shaders/`.
