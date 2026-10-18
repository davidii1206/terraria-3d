#pragma once
// Transport layer: reliable + unreliable UDP, connections, channels. DO NOT write this from scratch.
//
// Recommended library: ENet (small, simple, FetchContent-able) - or GameNetworkingSockets / yojimbo
// if encryption and NAT punch-through are required later. Wrap it behind this interface so the rest
// of the game never includes the library header.
//
// Channels
//   0  reliable ordered     handshake, chat, inventory changes, block edits, events, chunk data
//   1  unreliable sequenced entity snapshots / player inputs (old data is useless, drop it)
//   2  reliable unordered   bulk chunk streaming (so it never blocks the gameplay channel)
//
// Responsibilities: connect/disconnect/timeouts, ping/RTT stats, bandwidth limiting per connection,
// optional packet compression, MTU-safe fragmentation, a loopback transport for singleplayer
// (integrated server talks to the local client with zero sockets, same code path - important for testing),
// and a simulated latency/loss wrapper to test prediction locally.

#include "core/types.hpp"
#include <span>

namespace t3d
{
    class ITransport
    {
      public:
        virtual ~ITransport() = default;
        // TODO: poll events; send(connection, channel, bytes, reliable); disconnect(connection); stats(connection).
    };
} // namespace t3d
