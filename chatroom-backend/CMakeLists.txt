cmake_minimum_required(VERSION 3.10)

project(ChatRoom)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)

# ── Chat Server (:8000) ──
set(SERVER_SRCS
    server/main.cc
    server/database/MySQL.cc
    server/database/MySQLPool.cc
    server/database/Redis.cc
    server/email/EmailSender.cc
    server/model/FriendModel.cc
    server/model/GroupModel.cc
    server/model/User.cc
    server/model/UserModel.cc
    server/net/Connection.cc
    server/net/TcpServer.cc
    server/reactor/Reactor.cc
    server/service/ChatService.cc
)
add_executable(server ${SERVER_SRCS})
target_include_directories(server PRIVATE server ${MYSQL_INCLUDE_DIR})
target_link_libraries(server PRIVATE mysqlclient hiredis curl glog crypto)

# ── Client ──
add_executable(client
    client/main.cc
    client/common/common.cc
    client/net/TcpClient.cc
    client/net/net.cc
    client/page/auth.cc
    client/page/friend.cc
    client/page/group.cc
    client/page/settings.cc
)
target_include_directories(client PRIVATE client)
target_link_libraries(client PRIVATE readline pthread)

# ── Transit (WebSocket ↔ TCP) ──
add_executable(transit transit/transit.cc)
target_include_directories(transit PRIVATE transit)
target_link_libraries(transit PRIVATE Threads::Threads crypto)

# ── Web (HTTP 前端) ──
add_executable(web web/main.cc)
target_include_directories(web PRIVATE web)
target_link_libraries(web PRIVATE Threads::Threads)

add_custom_command(OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
    COMMENT "Copy index.html into bin directory"
)
add_custom_target(web_assets ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html)
