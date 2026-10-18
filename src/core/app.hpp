#pragma once
// Top-level application: picks a run mode, owns the subsystems, runs the main loop.
//
// RUN MODES (same binary, chosen on the command line, see main.cpp)
//   Standalone      window + renderer + client + an *integrated server* running on its own thread
//                   (singleplayer and "host a game"). Client talks to it over the loopback transport.
//   Client          window + renderer + client; connects to a remote server.
//   DedicatedServer no window, no renderer, no audio - just the server loop. Runs on a VPS.
//
// SUBSYSTEM OWNERSHIP / THREADS
//   main thread    window events, input, client update, rendering, UI
//   server thread  Server::tick at SERVER_TICK_RATE (Standalone/DedicatedServer only)
//   job threads    worldgen, meshing, lighting, save/load, compression (core/job_system.hpp)
//   audio thread   owned by the audio library
//   net thread(s)  optional: transport polling if the library does not do it in tick()
//   Rule: threads exchange data through message queues / immutable snapshots, never shared mutable state.
//
// CLIENT FRAME (variable dt)         SERVER TICK (fixed dt)
//   poll window/input                  receive packets
//   client.update(dt)                  world + entity systems
//   build RenderView                   replication / send packets
//   renderer.render(view)              autosave, housekeeping
//
// STARTUP ORDER:  config -> registries (blocks, items, recipes, mobs, biomes) -> textures/models ->
//   [server: load or generate world] -> [client: window, renderer, audio, UI, connect]
// SHUTDOWN:  disconnect clients -> save world (flush job system) -> stop threads -> destroy GPU objects.

#include "core/config.hpp"
#include "core/types.hpp"

namespace t3d
{
    enum class RunMode { Standalone, Client, DedicatedServer };

    struct AppArgs
    {
        RunMode mode = RunMode::Standalone;
        ClientSettings client;
        ServerSettings server;
        // TODO: connect address for Client mode, --world <name>, --seed, --port, --headless-test.
    };

    class App
    {
      public:
        explicit App(AppArgs args);
        auto run() -> int;

      private:
        AppArgs args_;
        auto run_client() -> int;
        auto run_dedicated_server() -> int;
    };
} // namespace t3d
