# TCP Examples

## Examples Structure

- `sync/` — Synchronous (blocking) examples. Uses `start_sync()` for simplicity.
- `async/` — Asynchronous (callback-driven) examples. Shows non-blocking patterns.

## Binaries

| Binary | Description |
|--------|-------------|
| `sync_tcp_echo_server` | Blocking echo server |
| `sync_tcp_echo_client` | Interactive blocking client |
| `async_tcp_echo_server`| Event-driven server with status heartbeat |
| `async_tcp_echo_client`| Event-driven client with parallel input handling |
| `async_tcp_telemetry_server` | Custom framing, backpressure, and RuntimeStats reporting |
| `async_tcp_telemetry_client` | Load generator for the telemetry server |

## Usage

```bash
# Echo pair (default port 8080)
./build/fetchcontent/bin/sync_tcp_echo_server [port]
./build/fetchcontent/bin/sync_tcp_echo_client [host] [port]

# Broadcast server - connect multiple clients to see messages relayed
./build/fetchcontent/bin/sync_tcp_broadcast_server [port]
./build/fetchcontent/bin/sync_tcp_echo_client 127.0.0.1 8080
```

Type messages in any client terminal. `/quit` disconnects.

## Framing, Backpressure, and Stats

The telemetry pair goes past the echo examples: it defines a length-prefixed
wire format in `async/telemetry_protocol.hpp`, feeds it to the server through
`framer()`, and prints `RuntimeStats` while traffic is flowing.

```bash
# Terminal 1 - default port 9100
./build/fetchcontent/bin/async_tcp_telemetry_server [port]

# Terminal 2 - 20000 records at 5000/s, comfortably within capacity
./build/fetchcontent/bin/async_tcp_telemetry_client 127.0.0.1 9100 20000 5000

# Unthrottled and long enough to make the server start dropping replies
./build/fetchcontent/bin/async_tcp_telemetry_client 127.0.0.1 9100 1500000 0
```

The unthrottled run produces a server snapshot along these lines - every frame
decoded, and the reply stream trimmed to fit the queue budget:

```text
[stats] clients=1 decoded=1500000 malformed=0
        in  : 795000000 B over 219452 socket reads
        out : 390296240 B accepted -> 390296240 B written (736408 acks queued, 100444 writes) failed_sends=0
        drop: 404703760 B / 763592 msgs bp_events=0
        queue: 0 B now, 261820 B peak
```

Points worth reading the source for:

- **Length prefix over delimiters.** `LineFramer` and `PacketFramer` scan for a
  byte pattern, which a binary payload can contain by accident. Protobuf-style
  messages need a length prefix, which is what the custom `IFramer` implements.
- **`on_message()` vs `on_data()`.** `on_data()` fires once per socket read;
  `on_message()` fires once per complete frame. The server's `decoded` counter
  next to `messages_received` shows how far apart the two run.
- **`try_send_to()` inside a callback.** A blocking `send_to()` from the io
  thread would deadlock the channel it waits on. Refused acks surface as
  `failed_sends`.
- **Which strategy notifies you.** The server runs `BestEffort` and the client
  runs the `Reliable` default, and they report congestion through different
  fields. Reliable blocks the sender, so the queue reaches the threshold and
  `on_backpressure()` fires (`backpressure_events` climbs, nothing is dropped).
  BestEffort drops to stay *under* that threshold, so `on_backpressure()` never
  fires and `backpressure_events` stays at 0 while `dropped_bytes` climbs. Wire
  up the callback and you will still be blind to BestEffort loss - watch
  `dropped_messages` for that.
- **Reading RuntimeStats.** `*_accepted` is what the application handed over,
  `*_sent` is what the socket wrote, `*_received` counts read completions. Byte
  totals line up across the first two; message counts do not, because writes are
  batched.

Note that a server's `stats()` aggregates its **live** sessions only, so the
counters drop back to zero once every client has disconnected. Keep your own
application-level counters if you need totals that survive connection churn.

## API Patterns

- `send_to(client_id, data)` — reply to a specific client (echo server)
- `try_send_to(client_id, data)` — non-blocking reply, safe inside callbacks
- `broadcast(data)` — send to all connected clients (broadcast server)
- `start_sync()` — block until the server is listening or failed
- `framer(factory)` + `on_message(handler)` — message framing (telemetry server)
- `backpressure_strategy(...)` + `on_backpressure(handler)` — queue policy
- `stats()` — `RuntimeStats` snapshot for the channel or server

## Troubleshooting

```bash
# Check if a port is in use
ss -tlnp | grep :8080

# Run on a different port
./build/fetchcontent/bin/sync_tcp_echo_server 9001
./build/fetchcontent/bin/sync_tcp_echo_client 127.0.0.1 9001
```
