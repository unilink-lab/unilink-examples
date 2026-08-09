# Examples

The examples are grouped by transport and then by execution style.

Commands below assume you are running them from the repository root after a
successful FetchContent preset build. All binaries are generated under
`build/fetchcontent/bin`.

## Quick Start

```bash
cmake -S . -B build
cmake --build build
```

## Categories

| Category | Details | Binaries |
|---|---|---|
| [TCP](tcp/) | Client/server streams over TCP | `sync_tcp_echo_server`, `sync_tcp_echo_client`, `sync_tcp_broadcast_server`, `async_tcp_echo_server`, `async_tcp_echo_client`, `async_tcp_telemetry_server`, `async_tcp_telemetry_client` |
| [UDP](udp/) | Datagram sender/receiver examples | `sync_udp_receiver`, `sync_udp_sender`, `async_udp_receiver`, `async_udp_sender` |
| [UDS](uds/) | Unix Domain Socket local IPC | `sync_uds_echo_server`, `sync_uds_echo_client`, `async_uds_echo_server`, `async_uds_echo_client` |
| [Serial](serial/) | Serial port echo examples | `sync_serial_echo`, `async_serial_echo` |

## Common Run Patterns

TCP echo:

```bash
# Terminal 1
./build/fetchcontent/bin/sync_tcp_echo_server

# Terminal 2
./build/fetchcontent/bin/sync_tcp_echo_client
```

UDP sender and receiver:

```bash
# Terminal 1
./build/fetchcontent/bin/sync_udp_receiver

# Terminal 2
./build/fetchcontent/bin/sync_udp_sender
```

UDS echo:

```bash
# Terminal 1
./build/fetchcontent/bin/sync_uds_echo_server

# Terminal 2
./build/fetchcontent/bin/sync_uds_echo_client
```

Serial echo:

```bash
./build/fetchcontent/bin/sync_serial_echo /dev/ttyUSB0 115200
```

Framing, backpressure, and RuntimeStats (see [TCP](tcp/) for the walkthrough):

```bash
# Terminal 1
./build/fetchcontent/bin/async_tcp_telemetry_server

# Terminal 2
./build/fetchcontent/bin/async_tcp_telemetry_client 127.0.0.1 9100 20000 5000
```

Serial examples require an available serial device or a virtual serial pair.
