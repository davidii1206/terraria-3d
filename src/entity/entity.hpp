#pragma once
// Entities: anything that lives in the world but isn't a block.
//
// ARCHITECTURE: use an ECS (e.g. EnTT, fetched via CMake FetchContent) with plain data components;
// systems run in a fixed order each server tick. Rationale: mobs, NPCs, players, projectiles
// and item drops share most behaviour (transform, physics, health) and many are numerous.
//
// Components (planned)
//   Transform          position (dvec3), yaw/pitch, previous position for render interpolation
//   Velocity, PhysicsBody (AABB size, gravity scale, on_ground, in_liquid)
//   Health             hp, max, invulnerability frames, armor/defense, knockback resist
//   NetworkId          EntityId + owner PlayerId + "replicate to clients?" flags + dirty mask
//   Player             PlayerId, name, gamemode, spawn point, permissions
//   Inventory          see game/inventory.hpp
//   Mob / AIState      see entity/mob.hpp
//   Projectile         owner, damage, lifetime, pierce count
//   ItemDrop           ItemStack, pickup delay, despawn timer, merge radius
//   Renderable         model/skeleton id, animation state, tint (flash on hit), scale
//   Lifetime, Light emitter (glowing mobs/items), SoundEmitter, StatusEffects (buffs/debuffs)
//
// Systems (server tick order): input -> AI -> movement/physics -> combat/damage -> status effects ->
//   spawning/despawning -> pickup/interaction -> replication (net/replication.hpp)
//
// Rendering side: client-only "view" entities are built from replicated components and interpolated
// between the last two server snapshots (~100 ms behind) for smooth motion.

namespace t3d
{
    // TODO: components + systems as described above.
} // namespace t3d
