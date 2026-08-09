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

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wirestead/wirestead.hpp"

/**
 * @file telemetry_protocol.hpp
 * @brief Wire format shared by the framed telemetry server and client.
 *
 * The format is a 4-byte big-endian length prefix followed by that many
 * payload bytes:
 *
 *     +--------+--------+--------+--------+---------------------+
 *     |            length (uint32, BE)    |  payload (length B) |
 *     +--------+--------+--------+--------+---------------------+
 */

namespace telemetry {

/// Size of the length prefix that precedes every payload.
inline constexpr size_t kPrefixSize = 4;

/// Largest payload this protocol accepts. Anything longer means the stream is
/// desynchronised (or the peer is hostile), so the framer resynchronises.
inline constexpr size_t kMaxFrameSize = 64 * 1024;

/**
 * @brief Framer for a 4-byte big-endian length prefix followed by a payload.
 *
 * The bundled LineFramer and PacketFramer both scan for a byte pattern, which a
 * binary payload can contain by accident. A length prefix has no such
 * ambiguity, which is why Protobuf, FlatBuffers, and CBOR streams are normally
 * delimited this way. Supplying it through `framer()` is all that is needed -
 * the server builds one framer per accepted client and hands each complete
 * payload to `on_message()`.
 */
class LengthPrefixedFramer : public wirestead::framer::IFramer {
 public:
  explicit LengthPrefixedFramer(size_t max_frame_size = kMaxFrameSize) : max_frame_size_(max_frame_size) {}

  void push_bytes(wirestead::memory::ConstByteSpan data) override {
    buffer_.insert(buffer_.end(), data.begin(), data.end());

    // A single read can deliver several frames, part of a frame, or both.
    while (true) {
      if (buffer_.size() < kPrefixSize) return;

      const size_t length = (static_cast<size_t>(buffer_[0]) << 24) | (static_cast<size_t>(buffer_[1]) << 16) |
                            (static_cast<size_t>(buffer_[2]) << 8) | static_cast<size_t>(buffer_[3]);

      if (length > max_frame_size_) {
        // Nothing in the buffer can be trusted once the prefix is nonsense.
        reset();
        return;
      }

      if (buffer_.size() < kPrefixSize + length) return;  // Frame not complete yet.

      if (on_message_) {
        on_message_(wirestead::memory::ConstByteSpan(buffer_.data() + kPrefixSize, length));
      }

      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kPrefixSize + length));
    }
  }

  void on_message(MessageCallback cb) override { on_message_ = std::move(cb); }

  /// Called by the library on connection loss, and by us on a bad prefix.
  void reset() override { buffer_.clear(); }

 private:
  size_t max_frame_size_;
  std::vector<uint8_t> buffer_;
  MessageCallback on_message_;
};

/**
 * @brief The application payload carried inside one frame.
 *
 * Hand-rolled so this example builds with no schema compiler in the loop. A
 * real deployment would replace encode/decode below with, say,
 * `MyMessage::ParseFromArray(payload.data(), payload.size())`. The framer above
 * is what makes that call safe: it guarantees `payload` is exactly one whole
 * message, never a partial one and never two concatenated.
 */
struct Record {
  uint16_t sensor_id = 0;
  uint32_t sequence = 0;
  double value = 0.0;
};

/// Byte length of an encoded Record.
inline constexpr size_t kRecordSize = sizeof(uint16_t) + sizeof(uint32_t) + sizeof(double);

/// Serialise a Record into its payload bytes (host byte order, for brevity - a
/// real protocol would pin the byte order the way the length prefix does).
inline std::string encode_record(const Record& record) {
  std::string payload(kRecordSize, '\0');
  std::memcpy(payload.data(), &record.sensor_id, sizeof(record.sensor_id));
  std::memcpy(payload.data() + sizeof(record.sensor_id), &record.sequence, sizeof(record.sequence));
  std::memcpy(payload.data() + sizeof(record.sensor_id) + sizeof(record.sequence), &record.value, sizeof(record.value));
  return payload;
}

/// Parse payload bytes back into a Record. Returns false if the payload is too
/// short - the framer guarantees whole payloads, not valid ones.
///
/// Trailing bytes past kRecordSize are ignored rather than rejected, the same
/// way a schema-based decoder skips fields it does not know about. The bundled
/// client relies on this to pad frames up to a realistic size.
inline bool decode_record(std::string_view payload, Record& out) {
  if (payload.size() < kRecordSize) return false;
  std::memcpy(&out.sensor_id, payload.data(), sizeof(out.sensor_id));
  std::memcpy(&out.sequence, payload.data() + sizeof(out.sensor_id), sizeof(out.sequence));
  std::memcpy(&out.value, payload.data() + sizeof(out.sensor_id) + sizeof(out.sequence), sizeof(out.value));
  return true;
}

/// Prepend the length prefix, producing bytes that are ready to send.
inline std::string frame(std::string_view payload) {
  const auto length = static_cast<uint32_t>(payload.size());
  std::string out;
  out.reserve(kPrefixSize + payload.size());
  out.push_back(static_cast<char>((length >> 24) & 0xFF));
  out.push_back(static_cast<char>((length >> 16) & 0xFF));
  out.push_back(static_cast<char>((length >> 8) & 0xFF));
  out.push_back(static_cast<char>(length & 0xFF));
  out.append(payload);
  return out;
}

}  // namespace telemetry
