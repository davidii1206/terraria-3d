#pragma once
// Combat and damage rules - all resolved on the server.
//
// Damage event: {attacker, victim, base damage, type (melee/ranged/magic/summon/fall/lava/drown/explosion),
//   knockback vector, crit flag, source item/projectile}.
// Pipeline: attacker modifiers (weapon, prefix, buffs, crit roll) -> victim defense (Terraria:
//   damage - defense/2, min 1) and resistances -> invulnerability frames -> apply health change ->
//   knockback impulse -> on death: loot table roll, drops, XP/score, respawn timer for players, event hooks.
// Melee: swing arcs approximated by a short-lived hitbox in front of the player over the weapon's use time;
//   hit each victim at most once per swing. Ranged/magic: spawn projectile entities (arrows with gravity,
//   magic bolts with none) that sweep-test blocks and entities each tick. Summons: minion entities with AI.
// PvP: flag per player or per team; friendly fire rules in ServerSettings.
// Explosions: remove blocks within a radius with a destroyable-by-explosion flag + damage entities (bombs, TNT).
// Hit confirmation for the local player is predicted visually (hit flash, sound) and corrected by the server if wrong.

namespace t3d
{
    // TODO: DamageEvent, apply_damage(), projectile system, hitbox helpers.
} // namespace t3d
