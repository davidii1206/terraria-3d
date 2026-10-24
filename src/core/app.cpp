#include "core/app.hpp"

#include "core/log.hpp"
#include "net/network_manager.hpp"
#include "net/steam_platform.hpp"
#include "platform/window.hpp"
#include "render/renderer.hpp"

namespace t3d
{
    App::App(AppArgs args) : args_{std::move(args)} {}

    auto App::run() -> int
    {
        switch (args_.mode)
        {
        case RunMode::DedicatedServer: return run_dedicated_server();
        case RunMode::Standalone:
            // TODO: start the integrated server thread + loopback transport, then fall through to the client.
        case RunMode::Client: return run_client();
        }
        return 0;
    }

    auto App::run_client() -> int
    {
        // TODO: load registries and assets before creating the renderer's GPU resources.

        // Order matters: Steam must outlive everything that uses its sockets, so declare it first
        // (destroyed last) and the network manager after it (destroyed first).
        net::SteamPlatform steam;
        if (!steam.init()) { log_info("steam unavailable ({}), using ENet only", steam.last_error()); }
        steam.parse_command_line(args_.argc, args_.argv);
        net::NetworkManager network;

        Window window{"terraria-3d", 1280, 720};
        Renderer renderer{window};

        while (!window.should_close())
        {
            window.poll_events();
            steam.update();
            network.update();
            if (auto host = steam.take_join_request()) { network.connect_steam(*host); } // overlay "Join Game"

            // TODO: input.sample(); client.update(dt); (prediction, interpolation, chunk meshing)
            // TODO: ui.update(); audio.update();

            RenderView view{};
            // TODO: client.fill_render_view(view);
            renderer.render(view);
        }
        return 0;
    }

    auto App::run_dedicated_server() -> int
    {
        // TODO: Server server; server.start(args_.server);
        //       loop: fixed-timestep server.tick() until a shutdown signal (SIGINT/SIGTERM) or "stop" command.
        log_error("dedicated server not implemented yet");
        return 1;
    }
} // namespace t3d
