#pragma once
// Input layer: raw GLFW events -> per-frame snapshot -> game *actions*.
//
// Layers:
//  1. Raw state (key down/pressed/released, mouse buttons, cursor delta, scroll, typed chars).
//  2. Bindings: user-configurable map  Action <- {key|mouse button|gamepad button}.
//  3. PlayerInput: the small struct the game actually consumes, and the one that is sent to the
//     server every tick (see net/protocol.hpp, PlayerInputPacket). It contains *intent*, never
//     positions: move axes, look yaw/pitch, jump/sprint/sneak, primary/secondary action,
//     hotbar slot, and a monotonically increasing sequence number for client-side prediction.
//
// UI focus: when a menu/chat/inventory is open the UI consumes input first and the world
// receives an empty PlayerInput (and the cursor is released).

#include "core/types.hpp"

namespace t3d
{
    enum class Action : u8
    {
        MoveForward, MoveBack, MoveLeft, MoveRight, Jump, Sneak, Sprint,
        Attack /*mine/hit*/, Use /*place/interact*/, Inventory, Chat, Drop, Pause,
        Hotbar1, /* ... */ HotbarNext, HotbarPrev,
        Count
    };

    struct PlayerInput
    {
        u32 sequence = 0;               // for reconciliation
        glm::vec2 move = {0, 0};        // x = strafe, y = forward, normalised
        f32 yaw = 0, pitch = 0;
        bool jump = false, sneak = false, sprint = false;
        bool attack = false, use = false;
        u8 hotbar_slot = 0;
    };

    // TODO: class Input { begin_frame(); bool pressed(Action); PlayerInput sample(); ... };
} // namespace t3d
