#pragma once
// Game client logic: everything between the network/world data and the renderer + UI.
//
// Per frame (variable timestep, vsync):
//   1. input: sample Input -> PlayerInput; if the UI has focus send an empty input
//   2. network: receive server packets -> update world cache, entity views, inventory, HUD state
//   3. prediction: run local movement for the controlled player with the unacknowledged input history;
//      reconcile on server corrections (see entity/player.hpp)
//   4. interpolation: other entities rendered at (latest snapshot time - interp delay)
//   5. world presentation: schedule meshing for chunks that changed/arrived, unload far chunks,
//      block selection raycast for the outline and mining crack overlay
//   6. fill RenderView (camera, sun from world time, visible chunk set, entity list, UI draw data) -> Renderer
//   7. audio: listener at the camera, one-shot events from the server, ambient/music by biome & depth
// Connection state machine: Disconnected -> Connecting -> Handshaking -> LoadingWorld (waiting for
//   spawn chunks, show progress screen) -> Playing -> Disconnecting. Show readable errors on failure/kick/timeout.

namespace t3d
{
    class Client
    {
      public:
        // TODO: connect(address)/disconnect(), update(dt), fill_render_view(RenderView&).
    };
} // namespace t3d
