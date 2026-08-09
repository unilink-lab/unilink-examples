# Copyright 2025 Jinwoo Sung
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

add_executable(sync_tcp_echo_server sync/echo_server.cc)
target_link_libraries(sync_tcp_echo_server PRIVATE wirestead::wirestead)
wirestead_configure_executable(sync_tcp_echo_server)

add_executable(sync_tcp_echo_client sync/echo_client.cc)
target_link_libraries(sync_tcp_echo_client PRIVATE wirestead::wirestead)
wirestead_configure_executable(sync_tcp_echo_client)

add_executable(sync_tcp_broadcast_server sync/broadcast_server.cc)
target_link_libraries(sync_tcp_broadcast_server PRIVATE wirestead::wirestead)
wirestead_configure_executable(sync_tcp_broadcast_server)

# Async Examples
add_executable(async_tcp_echo_client async/echo_client.cc)
target_link_libraries(async_tcp_echo_client PRIVATE wirestead::wirestead)
wirestead_configure_executable(async_tcp_echo_client)

add_executable(async_tcp_echo_server async/echo_server.cc)
target_link_libraries(async_tcp_echo_server PRIVATE wirestead::wirestead)
wirestead_configure_executable(async_tcp_echo_server)

# Framing, backpressure, and RuntimeStats
add_executable(async_tcp_telemetry_server async/telemetry_server.cc)
target_link_libraries(async_tcp_telemetry_server PRIVATE wirestead::wirestead)
wirestead_configure_executable(async_tcp_telemetry_server)

add_executable(async_tcp_telemetry_client async/telemetry_client.cc)
target_link_libraries(async_tcp_telemetry_client PRIVATE wirestead::wirestead)
wirestead_configure_executable(async_tcp_telemetry_client)
