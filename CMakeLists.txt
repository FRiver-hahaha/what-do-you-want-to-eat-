cmake_minimum_required(VERSION 3.16)

project(eat_transport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h)
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient)
if(MYSQL_INCLUDE_DIR AND MYSQL_CLIENT_LIB)
    set(EAT_MYSQL_FOUND TRUE)
else()
    set(EAT_MYSQL_FOUND FALSE)
    message(STATUS "mysqlclient not found: database-backed targets will be skipped")
endif()

add_library(eat_protocol STATIC
    shared/protocol/Crc32c.cc
    shared/protocol/FrameCodec.cc
    shared/protocol/JsonValue.cc
    shared/protocol/StrictJson.cc
)
target_include_directories(eat_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eat_protocol PUBLIC OpenSSL::Crypto)

add_library(eat_server_core STATIC
    server/app/BusinessExecutor.cc
    server/app/ThreadPool.cc
    server/app/Config.cc
    server/app/Log.cc
    server/app/Metrics.cc
    server/cache/CacheKeyBuilder.cc
    server/cache/CacheValue.cc
    server/cache/InvalidationSet.cc
    server/cache/SingleFlight.cc
    server/cache/TtlPolicy.cc
    server/identity/PasswordHasher.cc
    server/identity/CaptchaStore.cc
    server/identity/SessionRegistry.cc
    server/identity/TokenVerifier.cc
    server/tcp/ConnContext.cc
    server/tcp/FrameDispatcher.cc
    server/tcp/HeartbeatMonitor.cc
    server/tcp/RateLimiter.cc
    server/tcp/Backpressure.cc
)
target_include_directories(eat_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eat_server_core PUBLIC eat_protocol Threads::Threads)

if(NOT WIN32)
    target_sources(eat_server_core PRIVATE
        server/cache/CacheClient.cc
        server/database/RedisClient.cc
    )
endif()

if(EAT_MYSQL_FOUND)
    target_sources(eat_server_core PRIVATE
        server/database/MySQLPool.cc
        server/identity/IdentityService.cc
        server/business/profile/ProfileDao.cc
        server/business/profile/ProfileService.cc
        server/business/profile/ProfileMediaDao.cc
        server/business/profile/ProfileMediaService.cc
        server/business/user/UserDao.cc
        server/business/meal/MealDao.cc
        server/business/meal/IdempotencyDao.cc
        server/business/meal/MealService.cc
        server/business/stats/StatsService.cc
        server/business/food/FoodDao.cc
        server/business/food/FoodService.cc
        server/business/friend/FriendDao.cc
        server/business/friend/FriendService.cc
        server/business/ranking/RankingDao.cc
        server/business/ranking/RankingService.cc
        server/app/ServiceRegistry.cc
    )
    target_include_directories(eat_server_core PUBLIC ${MYSQL_INCLUDE_DIR})
    target_link_libraries(eat_server_core PUBLIC ${MYSQL_CLIENT_LIB})
endif()

enable_testing()

function(eat_register_test name)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
endfunction()

add_executable(frame_codec_test tests/protocol/frame_codec_test.cc)
target_link_libraries(frame_codec_test PRIVATE eat_protocol)
eat_register_test(frame_codec_test)

add_executable(strict_json_test tests/protocol/json_test.cc)
target_link_libraries(strict_json_test PRIVATE eat_protocol)
eat_register_test(strict_json_test)

add_executable(connection_governance_test tests/unit/connection_governance_test.cc)
target_link_libraries(connection_governance_test PRIVATE eat_server_core)
eat_register_test(connection_governance_test)

add_executable(thread_pool_test tests/unit/thread_pool_test.cc)
target_link_libraries(thread_pool_test PRIVATE eat_server_core)
eat_register_test(thread_pool_test)

add_executable(ipc_protocol_test
    tests/unit/ipc_protocol_test.cc
    client/ipc/IpcProtocol.cc
)
target_link_libraries(ipc_protocol_test PRIVATE eat_protocol)
eat_register_test(ipc_protocol_test)

add_executable(config_test tests/unit/config_test.cc)
target_link_libraries(config_test PRIVATE eat_server_core)
eat_register_test(config_test)

add_executable(frame_dispatcher_test tests/unit/frame_dispatcher_test.cc)
target_link_libraries(frame_dispatcher_test PRIVATE eat_server_core)
eat_register_test(frame_dispatcher_test)

add_executable(cache_test tests/unit/cache_test.cc)
target_link_libraries(cache_test PRIVATE eat_server_core)
eat_register_test(cache_test)

add_executable(identity_test tests/unit/identity_test.cc)
target_link_libraries(identity_test PRIVATE eat_server_core)
eat_register_test(identity_test)

add_executable(token_verifier_test tests/unit/token_verifier_test.cc)
target_link_libraries(token_verifier_test PRIVATE eat_server_core)
eat_register_test(token_verifier_test)

add_executable(offline_queue_test
    tests/unit/offline_queue_test.cc
    client/transport/OfflineQueue.cc
)
target_link_libraries(offline_queue_test PRIVATE eat_protocol Threads::Threads)
eat_register_test(offline_queue_test)

if(WIN32)
    add_executable(keyvault_test
        tests/unit/keyvault_test.cc
        client/keyvault/KeyVault.cc
    )
    target_link_libraries(keyvault_test PRIVATE eat_protocol OpenSSL::Crypto)
    eat_register_test(keyvault_test)
else()
    add_executable(keyvault_platform_test
        tests/unit/keyvault_platform_test.cc
        client/keyvault/KeyVault.cc
    )
    target_link_libraries(keyvault_platform_test PRIVATE eat_protocol OpenSSL::Crypto)
    eat_register_test(keyvault_platform_test)
endif()

add_executable(bench_tcp tests/load/bench_tcp/bench_tcp.cc)
target_link_libraries(bench_tcp PRIVATE eat_protocol Threads::Threads)
if(WIN32)
    target_link_libraries(bench_tcp PRIVATE ws2_32)
endif()

set(EAT_MUDUO_ROOT "" CACHE PATH "Muduo source or install root (contains muduo/net/TcpServer.h)")
if(NOT EAT_MUDUO_ROOT)
    if(DEFINED ENV{MUDUO_ROOT})
        set(EAT_MUDUO_ROOT "$ENV{MUDUO_ROOT}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/muduo/muduo/net/TcpServer.h")
        set(EAT_MUDUO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/muduo")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../muduo/muduo/net/TcpServer.h")
        set(EAT_MUDUO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../muduo")
    endif()
endif()

add_executable(client_smoke tests/integration/client_smoke.cc)
target_link_libraries(client_smoke PRIVATE eat_protocol Threads::Threads)
if(WIN32)
    target_link_libraries(client_smoke PRIVATE ws2_32)
endif()

if(WIN32)
    add_subdirectory(client/app)
endif()

if(EAT_MUDUO_ROOT AND EXISTS "${EAT_MUDUO_ROOT}/muduo/net/TcpServer.h" AND EAT_MYSQL_FOUND)
    find_library(MUDUO_NET_LIB
        NAMES muduo_net
        PATHS
            "${EAT_MUDUO_ROOT}/build/release-cpp11/lib"
            "${EAT_MUDUO_ROOT}/lib"
            "${EAT_MUDUO_ROOT}/lib64"
        NO_DEFAULT_PATH
    )
    find_library(MUDUO_BASE_LIB
        NAMES muduo_base
        PATHS
            "${EAT_MUDUO_ROOT}/build/release-cpp11/lib"
            "${EAT_MUDUO_ROOT}/lib"
            "${EAT_MUDUO_ROOT}/lib64"
        NO_DEFAULT_PATH
    )
    if(MUDUO_NET_LIB AND MUDUO_BASE_LIB)
        add_executable(eat-server
            server/tcp/main.cc
            server/tcp/TcpServerApp.cc
        )
        target_include_directories(eat-server PRIVATE "${EAT_MUDUO_ROOT}")
        target_link_libraries(eat-server PRIVATE
            eat_server_core "${MUDUO_NET_LIB}" "${MUDUO_BASE_LIB}" Threads::Threads)
        message(STATUS "eat-server enabled with Muduo at ${EAT_MUDUO_ROOT}")
    else()
        message(STATUS "eat-server skipped: Muduo libraries not found under ${EAT_MUDUO_ROOT}")
    endif()
else()
    message(STATUS "eat-server skipped: need Muduo (EAT_MUDUO_ROOT) and mysqlclient")
endif()
