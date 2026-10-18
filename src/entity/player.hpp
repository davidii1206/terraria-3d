#pragma once
// Player entity - the one shared by client prediction and server simulation.
//
// Server: for every connected player, each tick pop the queued PlayerInput packets, run
//   movement (physics/collision.hpp) with the input, handle actions (mine/place/use/attack), validate
//   everything (reach distance, cooldowns, inventory contains item, target block is editable), then
//   broadcast the result. NEVER trust the client for positions, damage, or item counts.
// Client: run the same movement code immediately for responsiveness (prediction), keep a ring buffer of
//   unacknowledged inputs; when the server snapshot arrives, rewind to the acknowledged state and replay
//   the remaining inputs (reconciliation). Other players are interpolated.
//
// Player state: position/velocity, look angles, health/mana/stamina, food? (Terraria has none), buffs,
//   inventory + armor + accessory slots + hotbar selection, spawn point (bed), respawn timer,
//   gamemode (survival/creative/spectator), permissions, stats (deaths, blocks mined...), team/pvp flag.
// Mining: hold attack on a block -> progress += tool_speed * dt / hardness (server tracks progress, the
//   client shows crack stage); on completion remove block + spawn drops. Tool tier gating comes from BlockDef.
// Placing: validate target is air/replaceable, not inside any entity AABB, supported if required.

namespace t3d
{
    // TODO: PlayerState, apply_input(), mining/placing helpers, death/respawn.
} // namespace t3d
