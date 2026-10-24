// Manual check of the Steam relay between two machines / two Steam accounts (cannot be automated, see net_loopback_test).
//   cmake --build build --target net_steam_pair
//   host machine:   cd build && ./net_steam_pair host            (prints its SteamID64)
//   other machine:  cd build && ./net_steam_pair join <steamid64>
// Both need the Steam client running and logged in. Ctrl-C quits.

#include "net/network_manager.hpp"
#include "net/steam_platform.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace t3d::net;
using namespace std::chrono_literals;

static std::atomic<bool> g_quit{false};

static char const * state_name(NetworkManager::State s)
{
    switch (s)
    {
    case NetworkManager::State::Idle: return "idle";
    case NetworkManager::State::Hosting: return "hosting";
    case NetworkManager::State::Connecting: return "connecting";
    case NetworkManager::State::Connected: return "connected";
    }
    return "?";
}

int main(int argc, char ** argv)
{
    bool const host = argc >= 2 && std::strcmp(argv[1], "host") == 0;
    bool const join = argc >= 3 && std::strcmp(argv[1], "join") == 0;
    if (!host && !join)
    {
        std::printf("usage: %s host | %s join <steamid64>\n", argv[0], argv[0]);
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, [](int) { g_quit = true; });

    SteamPlatform steam;
    if (!steam.init())
    {
        std::printf("steam init failed: %s\n", steam.last_error().c_str());
        return 1;
    }
    std::printf("logged in as '%s' (steamid64 %llu)\n", steam.local_name().c_str(), static_cast<unsigned long long>(steam.local_id()));

    NetworkManager net;
    net.set_options({.username = steam.local_name()});

    if (host)
    {
        if (!net.start_host({.kind = TransportKind::Steam, .port = 25565, .max_clients = 8}))
        {
            std::printf("start_host failed: %s\n", net.last_error().c_str());
            return 1;
        }
        steam.publish_join_info(true);
        std::printf("hosting. tell your friend to run:  ./net_steam_pair join %llu\n", static_cast<unsigned long long>(steam.local_id()));
    }
    else
    {
        auto const host_id = std::strtoull(argv[2], nullptr, 10);
        std::printf("connecting to %llu through the Steam relay...\n", static_cast<unsigned long long>(host_id));
        if (!net.connect_steam(host_id))
        {
            std::printf("connect failed to start: %s\n", net.last_error().c_str());
            return 1;
        }
    }

    auto last_state = NetworkManager::State::Idle;
    bool last_in_game = false;
    auto last_report = std::chrono::steady_clock::now();
    bool was_active = false;

    while (!g_quit)
    {
        steam.update();
        net.update();

        // Host: accept everybody (like a trivial game would) and report who joins/leaves.
        while (auto ev = net.poll_session_event())
        {
            if (ev->type == SessionEvent::Type::JoinRequested)
            {
                std::printf("[host] join request from '%s' (steamid64 %llu) -> accepted\n", ev->username.c_str(),
                            static_cast<unsigned long long>(ev->identity));
                net.server_session()->accept(ev->peer, {});
            }
            else { std::printf("[host] '%s' left\n", ev->username.c_str()); }
        }

        if (net.state() != last_state)
        {
            last_state = net.state();
            std::printf("state: %s\n", state_name(last_state));
        }
        if (net.in_game() != last_in_game)
        {
            last_in_game = net.in_game();
            std::printf("in_game: %s\n", last_in_game ? "yes (handshake accepted)" : "no");
        }
        was_active = was_active || net.is_active();
        if (was_active && !net.is_active())
        {
            std::printf("disconnected: %s\n", net.last_error().c_str());
            break;
        }

        auto const now = std::chrono::steady_clock::now();
        if (now - last_report > 2s)
        {
            last_report = now;
            auto const stats = net.stats();
            std::printf("  rtt %u ms, clients %zu, relay '%s', in %llu B, out %llu B\n", net.rtt_ms(), net.client_count(),
                        steam.relay_status().c_str(), static_cast<unsigned long long>(stats.bytes_in),
                        static_cast<unsigned long long>(stats.bytes_out));
        }
        std::this_thread::sleep_for(5ms);
    }

    net.disconnect();
    steam.shutdown();
    return 0;
}
