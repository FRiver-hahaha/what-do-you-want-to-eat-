#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "shared/protocol/FrameCodec.h"
#include "shared/protocol/protocol.h"

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) { closesocket(socket); }
int LastError() { return WSAGetLastError(); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void CloseSocket(Socket socket) { close(socket); }
int LastError() { return errno; }
#endif

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9443;
    int connections = 100;
    int handshake_timeout_ms = 3000;
    std::string token = "bench-token";
};

Options Parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) options.host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) options.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--connections" && i + 1 < argc) options.connections = std::stoi(argv[++i]);
        else if (arg == "--timeout-ms" && i + 1 < argc) options.handshake_timeout_ms = std::stoi(argv[++i]);
        else if (arg == "--token" && i + 1 < argc) options.token = argv[++i];
        else if (arg == "--help") {
            std::cout << "bench-tcp --host 127.0.0.1 --port 9443 --connections 100 [--token t]\n";
            std::exit(0);
        }
    }
    return options;
}

Socket Connect(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return kInvalidSocket;
    }
    Socket socket_handle = kInvalidSocket;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        socket_handle = static_cast<Socket>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (socket_handle == kInvalidSocket) continue;
        if (connect(socket_handle, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            break;
        }
        CloseSocket(socket_handle);
        socket_handle = kInvalidSocket;
    }
    freeaddrinfo(result);
    return socket_handle;
}

bool SendAll(Socket socket_handle, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = static_cast<int>(send(socket_handle, data.data() + sent,
                                            static_cast<int>(data.size() - sent), 0));
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "wsa_startup_failed\n";
        return 1;
    }
#endif
    const Options options = Parse(argc, argv);
    int ok = 0;
    int failed = 0;
    std::vector<Socket> sockets;
    sockets.reserve(static_cast<std::size_t>(options.connections));

    for (int i = 0; i < options.connections; ++i) {
        const Socket socket_handle = Connect(options.host, options.port);
        if (socket_handle == kInvalidSocket) {
            ++failed;
            continue;
        }
        const std::string body =
            "{\"access_token\":\"" + options.token +
            "\",\"device_id\":\"bench-" + std::to_string(i) +
            "\",\"client_version\":\"bench/1\",\"protocol_version\":{\"major\":1,\"minor\":0},"
            "\"capabilities\":[],\"max_frame_accept\":65536}";
        const std::string frame =
            eat::protocol::FrameCodec::Encode(eat::protocol::FrameType::HandshakeReq,
                                              static_cast<std::uint64_t>(i + 1), body);
        if (!SendAll(socket_handle, frame)) {
            CloseSocket(socket_handle);
            ++failed;
            continue;
        }
        sockets.push_back(socket_handle);
        ++ok;
    }

    std::cout << "connected=" << ok << " failed=" << failed
              << " host=" << options.host << " port=" << options.port << "\n";

    for (const Socket socket_handle : sockets) {
        CloseSocket(socket_handle);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return failed == 0 ? 0 : 2;
}