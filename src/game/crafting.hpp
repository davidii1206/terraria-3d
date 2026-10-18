#pragma once
// Crafting (Terraria style: recipes are available when the player is NEAR the required stations
// and has the ingredients in any container they carry - no crafting grid).
//
// Recipe (data/recipes/*.json): result item + count, ingredient list (item or *tag* such as
//   "any_wood", "any_iron_bar"), required stations (workbench, furnace, anvil, ...; stations are blocks
//   found within a radius of the player), optional conditions (biome, progression flag, near water/lava).
// RecipeBook: index by result and by ingredient for the UI ("what can I craft now", "what uses this item").
// Flow: client UI shows craftable recipes (computed locally for responsiveness) -> sends CraftRequest(recipe,
//   count) -> server re-validates stations + ingredients, consumes, adds the result (drops on the floor if
//   the inventory is full) -> replies with the inventory delta.
// Related processing: smelting in furnaces (block entity with progress + fuel), alchemy/brewing stations, etc.

namespace t3d
{
    // TODO: Recipe, RecipeBook, craft(), station detection.
} // namespace t3d
