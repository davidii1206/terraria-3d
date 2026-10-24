#pragma once
// Dear ImGui window to host / join / disconnect while developing.
// Call `draw_network_window(net, steam)` between ImGui::NewFrame() and ImGui::Render()
// (hook it into the debug overlay in ui/ui.hpp once the ImGui renderer is set up).

#include "net/network_manager.hpp"
#include "net/steam_platform.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>

namespace t3d::net
{
    inline void draw_network_window(NetworkManager & net, SteamPlatform & steam, bool * open = nullptr)
    {
        if (!ImGui::Begin("Network", open))
        {
            ImGui::End();
            return;
        }

        switch (net.state())
        {
        case NetworkManager::State::Idle: ImGui::TextUnformatted("State: idle"); break;
        case NetworkManager::State::Hosting: ImGui::Text("State: hosting (%zu clients)", net.client_count()); break;
        case NetworkManager::State::Connecting: ImGui::TextUnformatted("State: connecting..."); break;
        case NetworkManager::State::Connected: ImGui::TextUnformatted("State: connected"); break;
        }
        if (net.in_game()) { ImGui::Text("In game - ping %u ms", net.rtt_ms()); }
        else if (net.is_connected()) { ImGui::TextUnformatted("Connected, handshaking..."); }
        auto const traffic = net.stats();
        ImGui::Text("Traffic: in %llu B / %llu pkts, out %llu B / %llu pkts", static_cast<unsigned long long>(traffic.bytes_in),
                    static_cast<unsigned long long>(traffic.packets_in), static_cast<unsigned long long>(traffic.bytes_out),
                    static_cast<unsigned long long>(traffic.packets_out));
        if (!net.last_error().empty()) { ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Error: %s", net.last_error().c_str()); }

        if (SteamPlatform::available())
        {
            ImGui::Separator();
            ImGui::Text("Steam: %s (%llu)", steam.local_name().c_str(), static_cast<unsigned long long>(steam.local_id()));
            ImGui::Text("Relay: %s", steam.relay_status().c_str());
        }
        else if (!steam.last_error().empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("Steam unavailable: %s", steam.last_error().c_str());
        }
        ImGui::Separator();

        if (net.state() == NetworkManager::State::Idle)
        {
            static int transport = 0; // 0 = Steam (if available), 1 = ENet, 2 = local only (in-process)
            static char host[64] = "127.0.0.1";
            static char steam_id[32] = "";
            static int port = 25565;
            if (!SteamPlatform::available()) { transport = 1; }

            ImGui::BeginDisabled(!SteamPlatform::available());
            ImGui::RadioButton("Steam", &transport, 0);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::RadioButton("ENet (IP)", &transport, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Local only", &transport, 2);

            if (transport == 0) { ImGui::InputText("Host SteamID", steam_id, sizeof(steam_id), ImGuiInputTextFlags_CharsDecimal); }
            else if (transport == 1) { ImGui::InputText("Host", host, sizeof(host)); }

            // Fake network conditions for testing prediction / interpolation (applies to the next host/join).
            static int latency = 0, jitter = 0;
            static float loss = 0.0f;
            ImGui::SliderInt("Sim latency (ms, per direction)", &latency, 0, 400);
            ImGui::SliderInt("Sim jitter (ms)", &jitter, 0, 100);
            ImGui::SliderFloat("Sim loss (unreliable)", &loss, 0.0f, 0.5f);
            auto options = net.options();
            options.simulation = {.latency_ms = static_cast<std::uint32_t>(latency), .jitter_ms = static_cast<std::uint32_t>(jitter), .loss = loss};
            net.set_options(options);
            ImGui::InputInt(transport == 0 ? "Local port" : "Port", &port);
            port = std::clamp(port, 1, 65535);

            auto const kind = transport == 0 ? TransportKind::Steam : transport == 1 ? TransportKind::Enet : TransportKind::Loopback;
            if (ImGui::Button("Host"))
            {
                if (net.start_host({.kind = kind, .port = static_cast<std::uint16_t>(port)}) && kind == TransportKind::Steam)
                {
                    steam.publish_join_info(true);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Join"))
            {
                if (transport == 0) { net.connect_steam(std::strtoull(steam_id, nullptr, 10)); }
                else if (transport == 1) { net.connect(host, static_cast<std::uint16_t>(port)); }
                else { net.connect(Endpoint::loopback(static_cast<std::uint16_t>(port))); }
            }
        }
        else if (ImGui::Button("Disconnect"))
        {
            net.disconnect();
            steam.publish_join_info(false);
        }
        ImGui::End();
    }
} // namespace t3d::net
