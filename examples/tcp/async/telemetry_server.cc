/*
 * Copyright 2025 Jinwoo Sung
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "telemetry_protocol.hpp"
#include "wirestead/wirestead.hpp"

/**
 * @brief Framed telemetry ingest server.
 *
 * The echo examples cover the transport surface. This one covers the three
 * features you reach for once a real protocol is involved:
 *
 *   1. framer() / on_message() - a custom length-prefixed framer, so every
 *      callback receives exactly one whole application message.
 *   2. backpressure_strategy() / on_backpressure() - stale telemetry is worth
 *      less than fresh telemetry, so bound the queue and drop rather than
 *      stall the sender.
 *   3. stats() - read RuntimeStats to see what was accepted, dropped, and
 *      queued while it was happening.
 *
 * Pair it with `async_tcp_telemetry_client`, which can send fast enough to make
 * this server start dropping replies on purpose.
 */

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) { g_running.store(false); }

/// Queue budget per client. Deliberately small so the bundled client can drive
/// the server into backpressure without saturating a real network.
constexpr size_t kBackpressureThreshold = 256 * 1024;

}  // namespace

int main(int argc, char* argv[]) {
  uint16_t port = 9100;
  if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::cout << "--- Framed Telemetry Server ---\n";

  std::shared_ptr<wirestead::TcpServer> server;

  // Application-level counters. RuntimeStats counts what the transport moved;
  // these count what the application understood. Comparing the two is how you
  // tell a framing bug from a decoding bug.
  std::atomic<uint64_t> decoded{0};
  std::atomic<uint64_t> malformed{0};

  auto builder = wirestead::tcp_server(port);
  builder
      // (1) One framer instance is created per accepted client, so per-client
      //     partial-frame state never leaks between connections.
      .framer([]() { return std::make_unique<telemetry::LengthPrefixedFramer>(telemetry::kMaxFrameSize); })
      // (2) BestEffort drops the oldest queued bytes once the threshold is
      //     reached. The Reliable default would instead block a sender until
      //     the queue drains - correct for file transfer, wrong for telemetry.
      .backpressure_strategy(wirestead::base::constants::BackpressureStrategy::BestEffort)
      .backpressure_threshold(kBackpressureThreshold)
      .on_connect([](const wirestead::ConnectionContext& ctx) {
        std::cout << "[server] client connected: id=" << ctx.client_id() << "\n";
      })
      // on_message() fires once per complete frame. on_data() would fire once
      // per socket read, which is not the same thing and is the usual source of
      // "my messages arrive glued together" bugs.
      .on_message([&server, &decoded, &malformed](const wirestead::MessageContext& ctx) {
        telemetry::Record record;
        if (!telemetry::decode_record(ctx.data(), record)) {
          malformed.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        decoded.fetch_add(1, std::memory_order_relaxed);

        // Reply with the whole record we just consumed, so the response stream
        // is as large as the request stream. That is what lets a sender which
        // is too busy to drain its socket push this server's send queue past
        // the threshold above - with BestEffort, the overflow is dropped and
        // shows up in RuntimeStats::dropped_bytes rather than stalling anyone.
        //
        // try_send_to() rather than send_to(): we are inside a callback running
        // on the io thread, and a blocking send from here would deadlock the
        // channel it is waiting on. See docs/callbacks.md in the core
        // repository. A refused reply is expected under load and shows up in
        // RuntimeStats::failed_sends.
        if (server) {
          server->try_send_to(ctx.client_id(), telemetry::frame(ctx.data()));
        }
      })
      // (3) Fires when queue congestion changes, with the currently queued byte
      //     count. Worth knowing: this is not a reliable loss signal under
      //     BestEffort. Dropping is what holds the queue below the threshold
      //     that would fire it, so whether it fires depends on how the queue
      //     happens to cross that threshold - this server can discard hundreds
      //     of megabytes with backpressure_events still at 0. Watch
      //     RuntimeStats::dropped_bytes instead. Under the Reliable default -
      //     what the paired client uses - it fires and nothing is dropped.
      .on_backpressure(
          [](size_t queued_bytes) { std::cout << "[server] backpressure: " << queued_bytes << " B queued\n"; })
      .on_disconnect([](const wirestead::ConnectionContext& ctx) {
        std::cout << "[server] client disconnected: id=" << ctx.client_id() << "\n";
      })
      .on_error([](const wirestead::ErrorContext& ctx) { std::cerr << "[server] error: " << ctx.message() << "\n"; });

  server = builder.build();
  server->start();

  std::cout << "Listening on port " << port << ", backpressure threshold " << kBackpressureThreshold << " B\n";
  std::cout << "Press Ctrl+C to stop.\n\n";

  // Reading RuntimeStats correctly means knowing which layer each field counts:
  //
  //   *_accepted  what the application handed to the library
  //   *_sent      what the socket actually wrote (batching makes this a
  //               smaller message count than *_accepted, not a smaller byte count)
  //   *_received  socket read completions, not framed messages - compare it
  //               against `decoded` below to see framing at work
  //
  // Note also that a server's stats() aggregates its *live* sessions, so these
  // numbers fall back to zero once every client has disconnected.
  const auto report = [&](const char* label) {
    const wirestead::RuntimeStats stats = server->stats();
    std::cout << "[" << label << "] clients=" << server->connected_clients().size()  //
              << " decoded=" << decoded.load() << " malformed=" << malformed.load() << "\n"
              << "         in  : " << stats.bytes_received << " B over " << stats.messages_received << " socket reads\n"
              << "         out : " << stats.bytes_accepted << " B accepted -> " << stats.bytes_sent << " B written ("
              << stats.messages_accepted << " acks queued, " << stats.messages_sent << " writes)"
              << " failed_sends=" << stats.failed_sends << "\n"
              << "         drop: " << stats.dropped_bytes << " B / " << stats.dropped_messages << " msgs"
              << " bp_events=" << stats.backpressure_events << "\n"
              << "         queue: " << stats.queued_bytes << " B now, " << stats.max_queued_bytes << " B peak"
              << (stats.backpressure_active ? "  [ACTIVE]" : "") << "\n";
  };

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    report("stats");
  }

  std::cout << "\nShutting down.\n";
  report("final");
  server->stop();

  return 0;
}
