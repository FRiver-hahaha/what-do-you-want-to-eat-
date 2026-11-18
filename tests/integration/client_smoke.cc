#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "shared/protocol/FrameCodec.h"
#include "shared/protocol/JsonValue.h"
#include "shared/protocol/protocol.h"

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void CloseSocket(Socket socket) { close(socket); }
#endif

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9443;
    std::string token;
    std::string login;
    std::string password;
    std::string display_name = "Smoke User";
    bool do_register = false;
    double patch_weight = -1.0;
    bool omit_idem = false;
    std::string save_token;
    bool skip_handshake = false;
};

Options Parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) options.host = argv[++i];
        else if (arg == "--port" && i + 1 < argc)
            options.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--token" && i + 1 < argc) options.token = argv[++i];
        else if (arg == "--login" && i + 1 < argc) options.login = argv[++i];
        else if (arg == "--password" && i + 1 < argc) options.password = argv[++i];
        else if (arg == "--display-name" && i + 1 < argc) options.display_name = argv[++i];
        else if (arg == "--register") options.do_register = true;
        else if (arg == "--patch-weight" && i + 1 < argc)
            options.patch_weight = std::stod(argv[++i]);
        else if (arg == "--omit-idem") options.omit_idem = true;
        else if (arg == "--save-token" && i + 1 < argc) options.save_token = argv[++i];
        else if (arg == "--skip-handshake") options.skip_handshake = true;
        else if (arg == "--help") {
            std::cout << "client_smoke --host 127.0.0.1 --port 9443 "
                         "[--token t | --login u --password p [--register]]\n";
            std::exit(0);
        }
    }
    return options;
}

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

int ErrorCodeOf(const eat::protocol::JsonValue& value) {
    const eat::protocol::JsonValue* error = value.Find("error");
    if (error != nullptr && error->IsObject()) {
        const eat::protocol::JsonValue* code = error->Find("code");
        if (code != nullptr) return static_cast<int>(code->AsInt());
    }
    const eat::protocol::JsonValue* code = value.Find("code");
    return code != nullptr ? static_cast<int>(code->AsInt()) : -1;
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
    Socket handle = kInvalidSocket;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        handle = static_cast<Socket>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (handle == kInvalidSocket) continue;
        if (connect(handle, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) break;
        CloseSocket(handle);
        handle = kInvalidSocket;
    }
    freeaddrinfo(result);
#ifdef _WIN32
    if (handle != kInvalidSocket) {
        DWORD timeout = 3000;
        setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout));
    }
#else
    if (handle != kInvalidSocket) {
        timeval timeout{};
        timeout.tv_sec = 3;
        setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
#endif
    return handle;
}

bool SendAll(Socket handle, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int n = send(handle, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        const ssize_t n = send(handle, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool RecvFrame(Socket handle, eat::protocol::FrameCodec& codec,
               eat::protocol::Frame& frame, std::string& reason) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        eat::protocol::ErrorCode error = eat::protocol::ErrorCode::Ok;
        std::string parse_reason;
        const auto result = codec.Next(frame, error, parse_reason);
        if (result == eat::protocol::FrameCodec::Result::Ok) return true;
        if (result == eat::protocol::FrameCodec::Result::Error) {
            reason = parse_reason;
            return false;
        }
        char buffer[4096];
#ifdef _WIN32
        const int n = recv(handle, buffer, sizeof(buffer), 0);
#else
        const ssize_t n = recv(handle, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) {
            reason = "recv_failed";
            return false;
        }
        codec.Append(reinterpret_cast<const std::uint8_t*>(buffer), static_cast<std::size_t>(n));
    }
    reason = "frame_timeout";
    return false;
}

std::string HandshakeBody(const Options& options) {
    std::string body = "{";
    if (!options.token.empty()) {
        body += "\"access_token\":\"" + Escape(options.token) + "\",";
    } else {
        if (options.do_register) body += "\"action\":\"register\",";
        body += "\"login_id\":\"" + Escape(options.login) + "\",";
        body += "\"password\":\"" + Escape(options.password) + "\",";
        body += "\"display_name\":\"" + Escape(options.display_name) + "\",";
    }
    body += "\"device_id\":\"client-smoke\",\"client_version\":\"smoke/1\","
            "\"protocol_version\":{\"major\":1,\"minor\":0},"
            "\"capabilities\":[],\"max_frame_accept\":65536}";
    return body;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
#endif
    const Options options = Parse(argc, argv);
    if (options.token.empty() && (options.login.empty() || options.password.empty())) {
        std::cerr << "missing_credentials\n";
        return 2;
    }
    const Socket handle = Connect(options.host, options.port);
    if (handle == kInvalidSocket) {
        std::cerr << "connect_failed host=" << options.host << " port=" << options.port << "\n";
        return 3;
    }

    eat::protocol::FrameCodec codec(65536);
    std::string reason;
    if (options.skip_handshake) {
        const std::string wire = eat::protocol::FrameCodec::Encode(
            eat::protocol::FrameType::Request, 2, "{\"method\":\"profile.get\",\"params\":{}}");
        if (!SendAll(handle, wire)) {
            std::cerr << "request_send_failed\n";
            CloseSocket(handle);
            return 14;
        }
        eat::protocol::Frame early;
        if (!RecvFrame(handle, codec, early, reason)) {
            std::cerr << "pre_handshake_recv_failed reason=" << reason << "\n";
            CloseSocket(handle);
            return 15;
        }
        std::string early_reason;
        eat::protocol::JsonValue body = eat::protocol::JsonValue::Parse(early.body, early_reason);
        std::cout << "pre_handshake_error code=" << ErrorCodeOf(body)
                  << " frame_type=" << static_cast<int>(early.header.frame_type) << "\n";
        CloseSocket(handle);
        return 0;
    }
    const std::string handshake = eat::protocol::FrameCodec::Encode(
        eat::protocol::FrameType::HandshakeReq, 1, HandshakeBody(options));
    if (!SendAll(handle, handshake)) {
        std::cerr << "handshake_send_failed\n";
        CloseSocket(handle);
        return 4;
    }

    eat::protocol::Frame frame;
    std::string reason_unused;
    if (!RecvFrame(handle, codec, frame, reason_unused)) {
        std::cerr << "handshake_recv_failed reason=" << reason_unused << "\n";
        CloseSocket(handle);
        return 5;
    }
    std::string parse_reason;
    eat::protocol::JsonValue response = eat::protocol::JsonValue::Parse(frame.body, parse_reason);
    if (!parse_reason.empty() || !response.IsObject()) {
        std::cerr << "handshake_bad_json " << parse_reason << "\n";
        CloseSocket(handle);
        return 6;
    }
const int auth_code = ErrorCodeOf(response);
    if (auth_code >= 0) {
        std::cout << "handshake_error code=" << auth_code << "\n";
        CloseSocket(handle);
        return 7;
    }
    const eat::protocol::JsonValue* user_key = response.Find("user_key");
    std::string access_token;
    if (const auto* token = response.Find("access_token");
        token != nullptr && token->IsString()) {
        access_token = token->AsString();
    }
    std::cout << "handshake_ok user_key="
              << (user_key != nullptr ? user_key->AsString() : std::string())
              << " token=" << (access_token.empty() ? "provided" : "issued") << "\n";
    if (!options.save_token.empty() && !access_token.empty()) {
        std::ofstream out(options.save_token);
        out << access_token;
    }

    const std::string request = "{\"method\":\"profile.get\",\"params\":{}}";
    const std::string wire = eat::protocol::FrameCodec::Encode(
        eat::protocol::FrameType::Request, 2, request);
    if (!SendAll(handle, wire)) {
        std::cerr << "profile_send_failed\n";
        CloseSocket(handle);
        return 8;
    }
    eat::protocol::Frame profile_frame;
    if (!RecvFrame(handle, codec, profile_frame, reason)) {
        std::cerr << "profile_recv_failed reason=" << reason << "\n";
        CloseSocket(handle);
        return 9;
    }
    eat::protocol::JsonValue profile =
        eat::protocol::JsonValue::Parse(profile_frame.body, parse_reason);
    const auto* profile_ok = profile.Find("ok");
    if (profile_ok == nullptr || !profile_ok->AsBool(false)) {
        std::cout << "profile_error code=" << ErrorCodeOf(profile) << "\n";
        CloseSocket(handle);
        return 11;
    }
    const auto* data = profile.Find("data");
    const auto* name = data != nullptr ? data->Find("name") : nullptr;
    const auto* source = data != nullptr ? data->Find("source") : nullptr;
    std::cout << "profile_ok name=" << (name != nullptr ? name->AsString() : std::string())
              << " source=" << (source != nullptr ? source->AsString() : std::string())
              << "\n";

    if (options.patch_weight >= 0.0) {
        std::string params = "{\"weight\":" + std::to_string(options.patch_weight);
        if (!options.omit_idem) params += ",\"idem_key\":\"smoke-patch-1\"";
        params += "}";
        const std::string patch_request =
            "{\"method\":\"profile.patch\",\"params\":" + params + "}";
        const std::string patch_wire = eat::protocol::FrameCodec::Encode(
            eat::protocol::FrameType::Request, 3, patch_request);
        if (!SendAll(handle, patch_wire)) {
            std::cerr << "patch_send_failed\n";
            CloseSocket(handle);
            return 12;
        }
        eat::protocol::Frame patch_frame;
        if (!RecvFrame(handle, codec, patch_frame, reason)) {
            std::cerr << "patch_recv_failed reason=" << reason << "\n";
            CloseSocket(handle);
            return 13;
        }
        eat::protocol::JsonValue patch = eat::protocol::JsonValue::Parse(patch_frame.body, parse_reason);
        const auto* patch_ok = patch.Find("ok");
        if (patch_ok == nullptr || !patch_ok->AsBool(false)) {
            std::cout << "patch_error code=" << ErrorCodeOf(patch) << "\n";
        } else {
            std::cout << "patch_ok\n";
        }
    }

    const std::string verify_wire = eat::protocol::FrameCodec::Encode(
        eat::protocol::FrameType::Request, 4, "{\"method\":\"profile.get\",\"params\":{}}");
    if (SendAll(handle, verify_wire)) {
        eat::protocol::Frame verify_frame;
        if (RecvFrame(handle, codec, verify_frame, reason)) {
            eat::protocol::JsonValue verify =
                eat::protocol::JsonValue::Parse(verify_frame.body, parse_reason);
            const auto* data2 = verify.Find("data");
            const auto* weight = data2 != nullptr ? data2->Find("weight") : nullptr;
            if (weight != nullptr) {
                std::cout << "verify_weight=" << weight->AsDouble() << "\n";
            }
        }
    }
    CloseSocket(handle);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
