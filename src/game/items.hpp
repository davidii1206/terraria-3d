#pragma once
// Items and the item registry (data/items/*.json).
//
// ItemDef: name ("terraria:copper_pickaxe"), display name, icon/model, max stack size, rarity colour,
//   category (block, tool, weapon, armor, accessory, consumable, material, ammo, placeable-furniture),
//   and category-specific data:
//     - block item: BlockId to place (+ wall variant)
//     - tool: type (pickaxe/axe/hammer/shovel), tier, mining speed, durability or none (Terraria: none)
//     - weapon: damage, damage type (melee/ranged/magic/summon), use time, knockback, crit chance,
//               projectile id, mana cost, swing style (swing/stab/shoot/cast)
//     - armor: slot (head/chest/legs), defense, set-bonus id
//     - accessory: stat modifiers, special effects (double jump, speed boots...)
//     - consumable: heal/mana restore, buff applied, cooldown, summon item for bosses
//   Prefix/modifiers (Terraria "Legendary", "Sharp"): random roll stored on the ItemStack.
//
// ItemStack { ItemId id; u16 count; ItemMeta (prefix, durability, custom name, enchant/nbt-like blob) }
// Keep ItemStack small and trivially copyable; it's sent over the network and saved constantly.

#include "core/types.hpp"

namespace t3d
{
    struct ItemStack
    {
        ItemId id = 0;
        u16 count = 0;
        // TODO: u8 prefix; u16 durability; optional meta blob.
    };

    // TODO: ItemDef, ItemRegistry (same load-from-json pattern as BlockRegistry).
} // namespace t3d
