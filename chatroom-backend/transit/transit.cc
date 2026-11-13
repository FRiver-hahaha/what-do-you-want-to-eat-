//
// transit.cc — WebSocket ↔ TCP 全双工双向中转
//   - epoll (ET) 统一管理所有 fd
//   - 线程池仅做 SHA1，不碰 I/O
//   - 后端长连接，\n 分帧，纯传输不解析 payload
//

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pool.h"

// ============================================================================
// 常量
// ============================================================================
constexpr int WEB_PORT = 10000;
constexpr int BACKEND_PORT = 8000;
constexpr int FILE_PORT = 8001;  // 二进制文件服务端口
constexpr int MAX_EVENTS = 1024;
constexpr int BUFFER_SIZE = 65536;
constexpr int EPOLL_TIMEOUT = 1000;  // ms
constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr const char* FILE_UP = "@FILE|U|";
constexpr const char* FILE_DOWN = "@FILE|D|";
constexpr const char* FILE_RESP = "@FILE|R|";
constexpr const char* FILE_END = "@FILE|E|";

// ============================================================================
// 工具
// ============================================================================
static void epoll_add(int epfd, int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}
static void epoll_mod(int epfd, int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}
static void epoll_del(int epfd, int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

static std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned n = (unsigned(data[i]) << 16) |
                     (i + 1 < len ? unsigned(data[i + 1]) << 8 : 0) |
                     (i + 2 < len ? unsigned(data[i + 2]) : 0);
        out += T[(n >> 18) & 0x3F];
        out += T[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? T[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? T[n & 0x3F] : '=';
    }
    return out;
}

// ============================================================================
// WebSocket
// ============================================================================
enum class WsOp : uint8_t { CONT = 0, TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 0xA };

struct WsFrame {
    WsOp opcode;
    std::string payload;
    bool fin;
};

static std::string ws_accept_key(const std::string& client_key) {
    auto s = client_key + WS_GUID;
    unsigned char h[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)s.data(), s.size(), h);
    return base64_encode(h, SHA_DIGEST_LENGTH);
}

static std::string extract_ws_key(const std::string& req) {
    const char* pat = "Sec-WebSocket-Key: ";
    auto p = req.find(pat);
    if (p == std::string::npos)
        return "";
    p += strlen(pat);
    auto e = req.find("\r\n", p);
    if (e == std::string::npos)
        return "";
    while (p < e && req[p] == ' ')
        p++;
    while (e > p && req[e - 1] == ' ')
        e--;
    return req.substr(p, e - p);
}

static std::string ws_encode(WsOp op,
                             const std::string& payload,
                             bool fin = true) {
    std::string f;
    f.reserve(10 + payload.size());
    f.push_back((char)((fin ? 0x80 : 0) | (uint8_t)op));
    size_t len = payload.size();
    if (len < 126) {
        f.push_back((char)len);
    } else if (len <= 0xFFFF) {
        f.push_back((char)126);
        uint16_t n = htons((uint16_t)len);
        f.append((char*)&n, 2);
    } else {
        f.push_back((char)127);
        uint64_t n = htobe64(len);
        f.append((char*)&n, 8);
    }
    f += payload;
    return f;
}

static std::optional<WsFrame> ws_decode(const char* d,
                                        size_t len,
                                        size_t& consumed) {
    if (len < 2)
        return {};
    auto* p = (const uint8_t*)d;
    bool fin = (p[0] & 0x80), masked = (p[1] & 0x80);
    uint8_t op = p[0] & 0xF;
    uint64_t plen = p[1] & 0x7F;
    size_t hdr = 2;
    if (plen == 126) {
        if (len < 4)
            return {};
        uint16_t v;
        memcpy(&v, p + 2, 2);
        plen = ntohs(v);
        hdr = 4;
    } else if (plen == 127) {
        if (len < 10)
            return {};
        uint64_t v;
        memcpy(&v, p + 2, 8);
        plen = be64toh(v);
        hdr = 10;
    }
    uint8_t mk[4]{};
    if (masked) {
        if (len < hdr + 4)
            return {};
        memcpy(mk, p + hdr, 4);
        hdr += 4;
    }
    if (len < hdr + plen)
        return {};
    std::string payload;
    payload.resize(plen);
    for (size_t i = 0; i < plen; i++)
        payload[i] = p[hdr + i] ^ mk[i % 4];
    consumed = hdr + plen;
    return WsFrame{(WsOp)op, std::move(payload), fin};
}

// ============================================================================
// 连接状态机（精简为三态）
// ============================================================================
enum class State : uint8_t {
    HANDSHAKING,
    CONNECTING,
    ACTIVE,
};

struct Conn {
    int client_fd = -1;
    int server_fd = -1;   // 8000 聊天后端（常驻）
    int file_fd = -1;     // 8001 文件后端（文件传输期间存在）
    State state = State::HANDSHAKING;
    bool closing = false;

    std::vector<char> crbuf;
    std::string cwbuf;
    size_t cw_off = 0;

    std::string swbuf;
    size_t sw_off = 0;
    std::string srbuf;

    std::string frag_buf;  // 正在重组的 WebSocket 消息（FIN=0 分片累积）
    uint8_t frag_op = 0;   // 当前分片消息的起始 opcode（1=TEXT / 2=BINARY）

    // 文件隧道（复用 10000，@FILE 标记路由到 8001）
    bool file_connecting = false;    // file_fd 非阻塞连接中
    bool file_resp_started = false;  // 是否已下发 @FILE|R|
    std::string fswbuf;              // 待发往 8001 的原始字节
    size_t fsw_off = 0;

    bool http_parsed = false;
};

// ============================================================================
// 服务器
// ============================================================================
class TransitServer {
   public:
    explicit TransitServer(int pool_size = 4) : pool_(pool_size) {}

    int run() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (listen_fd_ < 0) {
            perror("socket");
            return 1;
        }
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                   sizeof(opt));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(WEB_PORT);
        if (bind(listen_fd_, (sockaddr*)&a, sizeof(a)) < 0) {
            perror("bind");
            return 1;
        }
        if (listen(listen_fd_, SOMAXCONN) < 0) {
            perror("listen");
            return 1;
        }

        epfd_ = epoll_create1(0);
        if (epfd_ < 0) {
            perror("epoll_create1");
            return 1;
        }
        epoll_add(epfd_, listen_fd_, EPOLLIN);

        std::cout << "[transit] WS :" << WEB_PORT
                  << " <-> backend :" << BACKEND_PORT << std::endl;

        epoll_event evs[MAX_EVENTS];
        while (run_flag_) {
            drain_callbacks();

            int n = epoll_wait(epfd_, evs, MAX_EVENTS, EPOLL_TIMEOUT);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; i++) {
                int fd = evs[i].data.fd;
                uint32_t ev = evs[i].events;

                if (fd == listen_fd_) {
                    handle_accept();
                    continue;
                }

                Conn* c = get_conn(fd);
                if (!c)
                    continue;

                if (fd == c->client_fd)
                    handle_client(c, ev);
                else if (fd == c->file_fd)
                    handle_file(c, ev);
                else
                    handle_server(c, ev);
            }
        }

        for (auto& [fd, c] : conns_) {
            epoll_del(epfd_, fd);
            close(fd);
            if (c->server_fd >= 0) {
                epoll_del(epfd_, c->server_fd);
                close(c->server_fd);
            }
            if (c->file_fd >= 0) {
                epoll_del(epfd_, c->file_fd);
                close(c->file_fd);
            }
        }
        close(listen_fd_);
        close(epfd_);
        return 0;
    }

   private:
    void handle_accept() {
        while (true) {
            int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                continue;
            }
            auto c = std::make_unique<Conn>();
            c->client_fd = fd;
            c->state = State::HANDSHAKING;
            epoll_add(epfd_, fd, EPOLLIN);
            conns_[fd] = std::move(c);
        }
    }

    void handle_client(Conn* c, uint32_t ev) {
        if (ev & (EPOLLERR | EPOLLHUP)) {
            close_conn(c->client_fd);
            return;
        }
        if (ev & EPOLLOUT)
            handle_client_write(c);
        if (ev & EPOLLIN)
            handle_client_read(c);
    }

    void handle_client_read(Conn* c) {
        char tmp[BUFFER_SIZE];
        while (true) {
            ssize_t n = recv(c->client_fd, tmp, sizeof(tmp), 0);
            if (n > 0)
                c->crbuf.insert(c->crbuf.end(), tmp, tmp + n);
            else if (n == 0) {
                close_conn(c->client_fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                close_conn(c->client_fd);
                return;
            }
        }

        if (c->state == State::HANDSHAKING)
            process_handshake(c);
        if (c->state == State::ACTIVE)
            process_ws_frames(c);
    }

    void handle_client_write(Conn* c) {
        while (c->cw_off < c->cwbuf.size()) {
            ssize_t n = send(c->client_fd, c->cwbuf.data() + c->cw_off,
                             c->cwbuf.size() - c->cw_off, MSG_NOSIGNAL);
            if (n > 0)
                c->cw_off += n;
            else if (n == 0) {
                close_conn(c->client_fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                close_conn(c->client_fd);
                return;
            }
        }

        if (c->cw_off >= c->cwbuf.size()) {
            c->cwbuf.clear();
            c->cw_off = 0;
            if (c->closing) {
                close_conn(c->client_fd);
                return;
            }
            epoll_mod(epfd_, c->client_fd, EPOLLIN);
        }
    }

    void handle_server(Conn* c, uint32_t ev) {
        if (c->state == State::CONNECTING) {
            if (ev & (EPOLLERR | EPOLLHUP)) {
                std::cerr << "[transit] backend connect failed" << std::endl;
                close_conn(c->client_fd);
                return;
            }
            if (ev & EPOLLOUT) {
                c->state = State::ACTIVE;
                epoll_mod(epfd_, c->server_fd, EPOLLIN);
                process_ws_frames(c);
            }
            return;
        }

        if (ev & (EPOLLERR | EPOLLHUP)) {
            close_conn(c->client_fd);
            return;
        }
        if (ev & EPOLLIN)
            handle_server_read(c);
        if (ev & EPOLLOUT)
            handle_server_write(c);
    }

    void handle_server_write(Conn* c) {
        while (c->sw_off < c->swbuf.size()) {
            ssize_t n = send(c->server_fd, c->swbuf.data() + c->sw_off,
                             c->swbuf.size() - c->sw_off, MSG_NOSIGNAL);
            if (n > 0)
                c->sw_off += n;
            else if (n == 0) {
                close_conn(c->client_fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                close_conn(c->client_fd);
                return;
            }
        }

        if (c->sw_off >= c->swbuf.size()) {
            c->swbuf.clear();
            c->sw_off = 0;
            epoll_mod(epfd_, c->server_fd, EPOLLIN);
        }
    }

    void handle_server_read(Conn* c) {
        char tmp[8192];
        while (true) {
            ssize_t n = recv(c->server_fd, tmp, sizeof(tmp), 0);
            if (n > 0) {
                c->srbuf.append(tmp, n);
            } else if (n == 0) {
                close_conn(c->client_fd);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                close_conn(c->client_fd);
                return;
            }
        }

        size_t pos;
        while ((pos = c->srbuf.find('\n')) != std::string::npos) {
            std::string line = c->srbuf.substr(0, pos);
            c->srbuf.erase(0, pos + 1);

            auto frame = ws_encode(WsOp::TEXT, line);
            c->cwbuf += frame;
            epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
        }
    }

    // ── 文件隧道（8001）────────────────────────────────────────────
    // 浏览器发 @FILE|U|/@FILE|D| 后，后续 BINARY 帧原样转发到 8001；
    // 8001 响应原样回传，首块前加 @FILE|R|、连接关闭时加 @FILE|E|。
    void open_file_backend(Conn* c) {
        end_file(c);
        c->file_fd = connect_backend_nb(FILE_PORT);
        if (c->file_fd < 0) {
            c->cwbuf += ws_encode(WsOp::TEXT, FILE_END);
            epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
            return;
        }
        c->file_connecting = true;
        c->file_resp_started = false;
        c->fswbuf.clear();
        c->fsw_off = 0;
        epoll_add(epfd_, c->file_fd, EPOLLOUT);
    }

    void handle_file(Conn* c, uint32_t ev) {
        if (c->file_connecting) {
            if (ev & (EPOLLERR | EPOLLHUP)) {
                end_file_with_notify(c);
                return;
            }
            if (ev & EPOLLOUT) {
                c->file_connecting = false;
                uint32_t want = EPOLLIN | (c->fswbuf.empty() ? 0 : EPOLLOUT);
                epoll_mod(epfd_, c->file_fd, want);
            }
            return;
        }
        if (ev & (EPOLLIN | EPOLLERR | EPOLLHUP))
            handle_file_read(c);
        if (ev & EPOLLOUT)
            handle_file_write(c);
    }

    void handle_file_read(Conn* c) {
        if (c->file_fd < 0)
            return;
        char tmp[8192];
        while (true) {
            ssize_t n = recv(c->file_fd, tmp, sizeof(tmp), 0);
            if (n > 0) {
                if (!c->file_resp_started) {
                    c->cwbuf += ws_encode(WsOp::TEXT, FILE_RESP);
                    c->file_resp_started = true;
                }
                c->cwbuf += ws_encode(WsOp::BINARY, std::string(tmp, n));
                epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
            } else if (n == 0) {
                end_file_with_notify(c);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                end_file_with_notify(c);
                return;
            }
        }
    }

    void handle_file_write(Conn* c) {
        if (c->file_fd < 0)
            return;
        while (c->fsw_off < c->fswbuf.size()) {
            ssize_t n = send(c->file_fd, c->fswbuf.data() + c->fsw_off,
                             c->fswbuf.size() - c->fsw_off, MSG_NOSIGNAL);
            if (n > 0)
                c->fsw_off += n;
            else if (n == 0) {
                end_file_with_notify(c);
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                end_file_with_notify(c);
                return;
            }
        }
        if (c->fsw_off >= c->fswbuf.size()) {
            c->fswbuf.clear();
            c->fsw_off = 0;
            epoll_mod(epfd_, c->file_fd, EPOLLIN);
        }
    }

    void end_file(Conn* c) {
        if (c->file_fd >= 0) {
            epoll_del(epfd_, c->file_fd);
            close(c->file_fd);
            c->file_fd = -1;
        }
        c->file_connecting = false;
        c->file_resp_started = false;
        c->fswbuf.clear();
        c->fsw_off = 0;
    }

    void end_file_with_notify(Conn* c) {
        end_file(c);
        c->cwbuf += ws_encode(WsOp::TEXT, FILE_END);
        epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
    }

    void process_handshake(Conn* c) {
        std::string data(c->crbuf.begin(), c->crbuf.end());
        auto pos = data.find("\r\n\r\n");
        if (pos == std::string::npos)
            return;

        std::string key = extract_ws_key(data);
        if (key.empty()) {
            std::cerr << "[transit] bad handshake" << std::endl;
            close_conn(c->client_fd);
            return;
        }

        c->http_parsed = true;
        c->crbuf.erase(c->crbuf.begin(), c->crbuf.begin() + pos + 4);

        int fd = c->client_fd;
        auto* self = this;
        pool_.enqueue([self, fd, key = std::move(key)]() {
            auto accept = ws_accept_key(key);
            self->post_callback([self, fd, accept = std::move(accept)]() {
                self->complete_handshake(fd, accept);
            });
        });
    }

    void complete_handshake(int client_fd, const std::string& accept_key) {
        auto it = conns_.find(client_fd);
        if (it == conns_.end())
            return;
        auto* c = it->second.get();

        std::string resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            accept_key + "\r\n\r\n";
        send(c->client_fd, resp.data(), resp.size(), MSG_NOSIGNAL);

        c->server_fd = connect_backend_nb(BACKEND_PORT);
        if (c->server_fd < 0) {
            std::cerr << "[transit] backend connect fail" << std::endl;
            close_conn(client_fd);
            return;
        }
        epoll_add(epfd_, c->server_fd, EPOLLOUT);
        c->state = State::CONNECTING;
    }

    void process_ws_frames(Conn* c) {
        if (c->state != State::ACTIVE)
            return;
        if (c->crbuf.empty())
            return;

        const char* d = c->crbuf.data();
        size_t len = c->crbuf.size(), total = 0;

        while (total < len) {
            size_t used = 0;
            auto f = ws_decode(d + total, len - total, used);
            if (!f)
                break;
            total += used;

            switch (f->opcode) {
                case WsOp::TEXT:
                case WsOp::BINARY:
                    c->frag_op = (uint8_t)f->opcode;  // 新分片消息开始
                    c->frag_buf = std::move(f->payload);
                    if (f->fin) complete_ws_message(c);
                    break;
                case WsOp::CONT:
                    c->frag_buf += f->payload;
                    if (f->fin) complete_ws_message(c);
                    break;
                case WsOp::CLOSE:
                    c->cwbuf += ws_encode(WsOp::CLOSE, "");
                    c->closing = true;
                    epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
                    goto done;
                case WsOp::PING:
                    c->cwbuf += ws_encode(WsOp::PONG, f->payload);
                    epoll_mod(epfd_, c->client_fd, EPOLLIN | EPOLLOUT);
                    break;
                case WsOp::PONG:
                    break;
            }
        }
    done:
        if (total > 0)
            c->crbuf.erase(c->crbuf.begin(), c->crbuf.begin() + total);
    }

    // 一条完整 WebSocket 消息（FIN=1 之后）。
    //  - BINARY 帧：文件模式下原样转发到 8001；否则忽略（聊天只走文本 JSON）
    //  - TEXT 帧：@FILE|U|/@FILE|D| 打开文件隧道；其余按聊天 JSON 转发到 8000
    void complete_ws_message(Conn* c) {
        uint8_t op = c->frag_op;
        std::string data = std::move(c->frag_buf);
        c->frag_buf.clear();
        c->frag_op = 0;

        if (op == (uint8_t)WsOp::BINARY) {
            if (c->file_fd >= 0) {
                c->fswbuf += data;
                epoll_mod(epfd_, c->file_fd, EPOLLIN | EPOLLOUT);
            }
            return;
        }
        if (data.rfind(FILE_UP, 0) == 0 || data.rfind(FILE_DOWN, 0) == 0) {
            open_file_backend(c);
            return;
        }
        forward_to_backend(c, std::move(data));
    }

    void forward_to_backend(Conn* c, std::string payload) {
        payload += '\n';
        c->swbuf += payload;
        epoll_mod(epfd_, c->server_fd, EPOLLIN | EPOLLOUT);
    }

    static int connect_backend_nb(int port) {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd < 0)
            return -1;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        int r = connect(fd, (sockaddr*)&a, sizeof(a));
        if (r < 0 && errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        return fd;
    }

    Conn* get_conn(int fd) {
        auto it = conns_.find(fd);
        if (it != conns_.end())
            return it->second.get();
        for (auto& [k, v] : conns_) {
            if (v->server_fd == fd)
                return v.get();
            if (v->file_fd == fd)
                return v.get();
        }
        return nullptr;
    }

    void close_conn(int client_fd) {
        auto it = conns_.find(client_fd);
        if (it == conns_.end())
            return;
        auto* c = it->second.get();
        epoll_del(epfd_, c->client_fd);
        close(c->client_fd);
        if (c->server_fd >= 0) {
            epoll_del(epfd_, c->server_fd);
            close(c->server_fd);
        }
        if (c->file_fd >= 0) {
            epoll_del(epfd_, c->file_fd);
            close(c->file_fd);
        }
        conns_.erase(it);
    }

    void post_callback(std::function<void()> cb) {
        {
            std::lock_guard<std::mutex> lk(cb_mtx_);
            callbacks_.push_back(std::move(cb));
        }
    }

    void drain_callbacks() {
        std::vector<std::function<void()>> cbs;
        {
            std::lock_guard<std::mutex> lk(cb_mtx_);
            cbs.swap(callbacks_);
        }
        for (auto& f : cbs)
            f();
    }

    int epfd_ = -1, listen_fd_ = -1;
    bool run_flag_ = true;
    pool pool_;

    std::map<int, std::unique_ptr<Conn>> conns_;

    std::vector<std::function<void()>> callbacks_;
    std::mutex cb_mtx_;
};

int main() {
    TransitServer srv;
    return srv.run();
}
