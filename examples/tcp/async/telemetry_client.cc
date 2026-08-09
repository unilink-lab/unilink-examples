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
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "telemetry_protocol.hpp"
#include "wirestead/wirestead.hpp"

/**
 * @brief Load generator for the framed telemetry server.
 *
 * Sends length-prefixed records as fast as the requested rate allows and
 * reports its own RuntimeStats. Run it with `--rate 0` to send unthrottled,
 * which is the quickest way to watch the server's drop and backpressure
 * counters move.
 *
 * Usage:
 *   async_tcp_telemetry_client [host] [port] [count] [rate_per_sec]
 *
 *   count        number of records to send (0 = run until Ctrl+C)
 *   rate_per_sec records per second (0 = unthrottled)
 */

namespace {

/// Payload padding, in bytes, added to each record so the queue fills at a
/// realistic rate rather than requiring millions of tiny messages.
constexpr size_t kPaddingBytes = 512;

}  // namespace

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  uint16_t port = 9100;
  uint64_t count = 20000;
  uint64_t rate_per_sec = 0;

  if (argc > 1) host = argv[1];
  if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
  if (argc > 3) count = std::stoull(argv[3]);
  if (argc > 4) rate_per_sec = std::stoull(argv[4]);

  std::cout << "--- Framed Telemetry Client ---\n";
  std::cout << "Target " << host << ":" << port << ", count=" << (count == 0 ? "unlimited" : std::to_string(count))
            << ", rate=" << (rate_per_sec == 0 ? "unthrottled" : std::to_string(rate_per_sec) + "/s") << "\n";

  std::shared_ptr<wirestead::TcpClient> client;
  std::atomic<bool> connected{false};
  std::atomic<uint64_t> acks{0};

  auto builder = wirestead::tcp_client(host, port);
  builder
      // The same framer the server uses - acks come back length-prefixed too,
      // so the client needs framing on its side as well.
      .framer([]() { return std::make_unique<telemetry::LengthPrefixedFramer>(telemetry::kMaxFrameSize); })
      // Reliable is the default and the right choice here: this is a load
      // generator, and silently dropping what we meant to send would make the
      // server-side numbers meaningless.
      .backpressure_strategy(wirestead::base::constants::BackpressureStrategy::Reliable)
      .on_connect([&connected](const wirestead::ConnectionContext&) {
        std::cout << "[client] connected\n";
        connected.store(true);
      })
      .on_message([&acks](const wirestead::MessageContext& ctx) {
        telemetry::Record record;
        if (telemetry::decode_record(ctx.data(), record)) {
          acks.fetch_add(1, std::memory_order_relaxed);
        }
      })
      .on_disconnect([&connected](const wirestead::ConnectionContext&) {
        std::cout << "[client] disconnected\n";
        connected.store(false);
      })
      .on_error([](const wirestead::ErrorContext& ctx) { std::cerr << "[client] error: " << ctx.message() << "\n"; });

  client = builder.build();
  client->start();

  // Wait briefly for the connection to come up before generating load.
  for (int i = 0; i < 50 && !connected.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!connected.load()) {
    std::cerr << "[client] could not connect to " << host << ":" << port << "\n";
    client->stop();
    return 1;
  }

  const std::string padding(kPaddingBytes, 'x');
  const auto started = std::chrono::steady_clock::now();
  uint64_t sent = 0;
  uint64_t refused = 0;

  for (uint64_t i = 0; count == 0 || i < count; ++i) {
    telemetry::Record record;
    record.sensor_id = static_cast<uint16_t>(i % 8);
    record.sequence = static_cast<uint32_t>(i);
    record.value = std::sin(static_cast<double>(i) / 100.0);

    // Payload is the encoded record plus padding, so one frame is a realistic
    // size. The framer on the server splits the stream back apart regardless of
    // how these bytes end up chunked by TCP.
    const auto payload = telemetry::encode_record(record) + padding;
    if (client->send(telemetry::frame(payload))) {
      ++sent;
    } else {
      ++refused;
    }

    if (rate_per_sec != 0) {
      const auto target = started + std::chrono::nanoseconds((i + 1) * 1000000000ULL / rate_per_sec);
      std::this_thread::sleep_until(target);
    }

    if (!connected.load()) {
      std::cout << "[client] connection lost after " << sent << " records\n";
      break;
    }
  }

  // Give in-flight acks a moment to arrive before reading the final numbers.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  const wirestead::RuntimeStats stats = client->stats();
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

  // messages_accepted counts our send() calls; messages_sent counts socket
  // writes. They differ because the library batches queued frames into fewer,
  // larger writes - the byte totals are what should line up, not the counts.
  std::cout << "\n[client] done in " << elapsed << " s\n"
            << "         app : sent=" << sent << " refused=" << refused << " acks=" << acks.load() << "\n"
            << "         out : " << stats.bytes_accepted << " B accepted -> " << stats.bytes_sent << " B written ("
            << stats.messages_accepted << " frames, " << stats.messages_sent << " writes)"
            << " failed_sends=" << stats.failed_sends << "\n"
            << "         in  : " << stats.bytes_received << " B over " << stats.messages_received << " socket reads\n"
            << "         drop: " << stats.dropped_bytes << " B / " << stats.dropped_messages << " msgs"
            << " bp_events=" << stats.backpressure_events << "\n"
            << "         queue peak: " << stats.max_queued_bytes << " B\n";

  client->stop();
  return 0;
}
