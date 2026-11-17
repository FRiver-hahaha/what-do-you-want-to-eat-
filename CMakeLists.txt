cmake_minimum_required(VERSION 3.16)

project(eat_transport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(eat_protocol STATIC
    shared/protocol/Crc32c.cc
    shared/protocol/FrameCodec.cc
    shared/protocol/JsonValue.cc
    shared/protocol/StrictJson.cc
)
target_include_directories(eat_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(eat_server_core STATIC
    server/app/BusinessExecutor.cc
    server/app/Config.cc
    server/app/Log.cc
    server/app/Metrics.cc
    server/cache/CacheKeyBuilder.cc
    server/cache/CacheValue.cc
    server/cache/InvalidationSet.cc
    server/cache/SingleFlight.cc
    server/cache/TtlPolicy.cc
    server/identity/PasswordHasher.cc
    server/identity/SessionRegistry.cc
    server/identity/TokenVerifier.cc
    server/tcp/ConnContext.cc
    server/tcp/FrameDispatcher.cc
    server/tcp/HeartbeatMonitor.cc
    server/tcp/RateLimiter.cc
    server/tcp/Backpressure.cc
)
target_include_directories(eat_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eat_server_core PUBLIC eat_protocol Threads::Threads crypto)

enable_testing()

add_executable(frame_codec_test tests/protocol/frame_codec_test.cc)
target_link_libraries(frame_codec_test PRIVATE eat_protocol)
add_test(NAME frame_codec_test COMMAND frame_codec_test)

add_executable(strict_json_test tests/protocol/json_test.cc)
target_link_libraries(strict_json_test PRIVATE eat_protocol)
add_test(NAME strict_json_test COMMAND strict_json_test)

add_executable(connection_governance_test tests/unit/connection_governance_test.cc)
target_link_libraries(connection_governance_test PRIVATE eat_server_core)
add_test(NAME connection_governance_test COMMAND connection_governance_test)

add_executable(config_test tests/unit/config_test.cc)
target_link_libraries(config_test PRIVATE eat_server_core)
add_test(NAME config_test COMMAND config_test)
add_executable(frame_dispatcher_test tests/unit/frame_dispatcher_test.cc)
target_link_libraries(frame_dispatcher_test PRIVATE eat_server_core)
add_test(NAME frame_dispatcher_test COMMAND frame_dispatcher_test)
add_executable(cache_test tests/unit/cache_test.cc)
target_link_libraries(cache_test PRIVATE eat_server_core)
add_test(NAME cache_test COMMAND cache_test)

add_executable(identity_test tests/unit/identity_test.cc)
target_link_libraries(identity_test PRIVATE eat_server_core)
add_test(NAME identity_test COMMAND identity_test)

add_executable(token_verifier_test tests/unit/token_verifier_test.cc)
target_link_libraries(token_verifier_test PRIVATE eat_server_core)
add_test(NAME token_verifier_test COMMAND token_verifier_test)

add_executable(offline_queue_test tests/unit/offline_queue_test.cc)
target_link_libraries(offline_queue_test PRIVATE eat_protocol Threads::Threads)
add_test(NAME offline_queue_test COMMAND offline_queue_test)

add_executable(bench_tcp tests/load/bench_tcp/bench_tcp.cc)
target_link_libraries(bench_tcp PRIVATE eat_protocol Threads::Threads)
if(WIN32)
    target_link_libraries(bench_tcp PRIVATE ws2_32)
endif()

find_path(MUDUO_INCLUDE_PATH muduo/net/TcpServer.h)
if(MUDUO_INCLUDE_PATH)
    add_executable(eat-server
        server/tcp/main.cc
        server/tcp/TcpServerApp.cc
    )
    target_include_directories(eat-server PRIVATE ${MUDUO_INCLUDE_PATH})
    target_link_libraries(eat-server PRIVATE eat_server_core muduo_net muduo_base)
endif()