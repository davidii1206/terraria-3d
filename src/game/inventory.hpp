#pragma once
// Inventory containers.
//
// Generic `Container` = vector<ItemStack> with: insert (merge into stacks then empty slots), remove,
// swap/move/split stack, can_insert checks, change listeners.
// Uses of Container: player main inventory (+ hotbar = first row), armor slots, accessory slots,
//   ammo slots, coin purse, piggy-bank/safe (per-player, any location), chests (block entities, shared
//   by players), furnace/crafting-station slots, NPC shop stock, trash slot, cursor-held stack.
//
// Networking: the SERVER owns every container. Client UI sends *intents* ("move slot 3 of container A
//   to slot 7 of container B", "split", "shift-click", "drop"); the server validates and replies with the
//   changed slots (delta, with a container revision number so stale intents get rejected and the client resyncs).
//   Chest open/close tracked server-side so only nearby players receive updates and two players can use one chest.
// Server-side checks: container still reachable (distance, block still exists), slot indices valid, item counts.

#include "game/items.hpp"
#include <vector>

namespace t3d
{
    class Container
    {
      public:
        // TODO: slots, insert/remove/move, listeners, revision counter.
      private:
        std::vector<ItemStack> slots_;
    };
} // namespace t3d
