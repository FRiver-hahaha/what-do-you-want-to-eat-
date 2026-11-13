#pragma once

// 与 file_server(8001) 之间的二进制文件传输，协议「文件头 + 文件名 + 二进制数据」。
// 与主服务端(8000) 的 \n 分隔 JSON 不同，这里用固定大小头 + 大端网络序。
// 上传用 sendfile 零拷贝直发；上传/下载均支持断点续传。

#include <arpa/inet.h>
#include <endian.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

constexpr uint16_t FILE_PORT = 8001;
constexpr char FILE_MAGIC[4] = {'F', 'I', 'L', 'E'};
constexpr uint8_t FILE_VERSION = 1;
constexpr uint8_t CMD_UPLOAD = 1;
constexpr uint8_t CMD_DOWNLOAD = 2;
constexpr uint8_t STATUS_OK = 0;
constexpr uint8_t STATUS_NOT_FOUND = 1;
constexpr size_t FILE_HEADER_SIZE = 28;
constexpr size_t FILE_RESP_SIZE = 24;
constexpr uint64_t FILE_SENDFILE_CHUNK = 1ULL << 20;  // 单次 sendfile 1MiB，兼顾进度粒度

// 进度回调：(已完成字节, 总字节)
using f_progress = std::function<void(uint64_t, uint64_t)>;

inline void f_store_be32(char* p, uint32_t v) {
    uint32_t n = htobe32(v);
    std::memcpy(p, &n, 4);
}
inline void f_store_be64(char* p, uint64_t v) {
    uint64_t n = htobe64(v);
    std::memcpy(p, &n, 8);
}
inline uint64_t f_load_be64(const char* p) {
    uint64_t n;
    std::memcpy(&n, p, 8);
    return be64toh(n);
}

inline bool f_send_all(int fd, const char* p, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n > 0) {
            p += n;
            len -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

inline bool f_recv_all(int fd, char* p, size_t len) {
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n > 0) {
            p += n;
            len -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

inline int f_connect(const std::string& host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &serv.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&serv), sizeof(serv)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// 是否为 "{file_id}_{name}" 引用形式（全数字 + 下划线前缀），用于区分 /put 的续传两参形式。
inline bool f_is_ref(const std::string& s) {
    if (s.empty() || s[0] < '0' || s[0] > '9') {
        return false;
    }
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    return i < s.size() && s[i] == '_' && i + 1 < s.size();
}

// 上传文件；返回 status（0=OK），-1 表示连接/IO 失败。
// 用 sendfile 零拷贝直发；续传偏移由服务端 .meta 决定（ack 里返回），客户端从该偏移继续发。
inline int f_upload(const std::string& host, const std::string& ref,
                    const std::string& path,
                    const f_progress& progress = nullptr) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return -1;
    }
    uint64_t file_size = static_cast<uint64_t>(st.st_size);

    int fd = f_connect(host, FILE_PORT);
    if (fd < 0) {
        return -1;
    }

    char h[FILE_HEADER_SIZE] = {0};
    std::memcpy(h, FILE_MAGIC, 4);
    h[4] = static_cast<char>(CMD_UPLOAD);
    h[7] = static_cast<char>(FILE_VERSION);
    f_store_be32(h + 8, static_cast<uint32_t>(ref.size()));
    f_store_be64(h + 12, file_size);
    f_store_be64(h + 20, 0);  // 上传续传由服务端 .meta 决定，offset 恒 0

    if (!f_send_all(fd, h, FILE_HEADER_SIZE) ||
        !f_send_all(fd, ref.data(), ref.size())) {
        close(fd);
        return -1;
    }

    char r[FILE_RESP_SIZE];
    if (!f_recv_all(fd, r, FILE_RESP_SIZE)) {
        close(fd);
        return -1;
    }
    if (std::memcmp(r, FILE_MAGIC, 4) != 0) {
        close(fd);
        return -1;
    }
    int status = static_cast<int>(static_cast<unsigned char>(r[5]));
    if (status != STATUS_OK) {
        close(fd);
        return status;
    }
    uint64_t resume = f_load_be64(r + 8);

    int file_fd = open(path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        close(fd);
        return -1;
    }

    off_t off = static_cast<off_t>(resume);
    uint64_t remaining = file_size - resume;

    int last_pct = -1;
    auto report = [&](uint64_t done) {
        if (!progress || file_size == 0) {
            return;
        }
        int pct = static_cast<int>(done * 100 / file_size);
        if (pct != last_pct) {
            last_pct = pct;
            progress(done, file_size);
        }
    };
    report(static_cast<uint64_t>(off));

    while (remaining > 0) {
        size_t count = static_cast<size_t>(
            std::min<uint64_t>(remaining, FILE_SENDFILE_CHUNK));
        ssize_t n = sendfile(fd, file_fd, &off, count);
        if (n > 0) {
            remaining -= static_cast<uint64_t>(n);
            report(static_cast<uint64_t>(off));
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EINVAL || errno == ENOSYS) {
            // 文件系统/socket 不支持 sendfile，回退 read+send
            char buf[65536];
            while (remaining > 0) {
                size_t want = static_cast<size_t>(
                    std::min<uint64_t>(remaining, sizeof(buf)));
                ssize_t rd = pread(file_fd, buf, want, off);
                if (rd < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    close(file_fd);
                    close(fd);
                    return -1;
                }
                if (rd == 0) {
                    break;
                }
                if (!f_send_all(fd, buf, static_cast<size_t>(rd))) {
                    close(file_fd);
                    close(fd);
                    return -1;
                }
                remaining -= static_cast<uint64_t>(rd);
                off += rd;
                report(static_cast<uint64_t>(off));
            }
            break;
        } else {
            close(file_fd);
            close(fd);
            return -1;
        }
    }
    report(file_size);
    close(file_fd);
    close(fd);
    return STATUS_OK;
}

// 下载文件；offset 为本地已下字节（断点续传）。返回 status（0=OK，1=不存在），
// -1 表示连接/IO 失败，data 填本次收到的字节。
inline int f_download(const std::string& host, const std::string& ref,
                      uint64_t offset, std::string& data,
                      const f_progress& progress = nullptr) {
    int fd = f_connect(host, FILE_PORT);
    if (fd < 0) {
        return -1;
    }

    char h[FILE_HEADER_SIZE] = {0};
    std::memcpy(h, FILE_MAGIC, 4);
    h[4] = static_cast<char>(CMD_DOWNLOAD);
    h[7] = static_cast<char>(FILE_VERSION);
    f_store_be32(h + 8, static_cast<uint32_t>(ref.size()));
    f_store_be64(h + 20, offset);  // 断点续传：从本地已有字节处继续

    if (!f_send_all(fd, h, FILE_HEADER_SIZE) ||
        !f_send_all(fd, ref.data(), ref.size())) {
        close(fd);
        return -1;
    }

    char r[FILE_RESP_SIZE];
    if (!f_recv_all(fd, r, FILE_RESP_SIZE)) {
        close(fd);
        return -1;
    }
    if (std::memcmp(r, FILE_MAGIC, 4) != 0) {
        close(fd);
        return -1;
    }
    int status = static_cast<int>(static_cast<unsigned char>(r[5]));
    if (status != STATUS_OK) {
        close(fd);
        return status;
    }
    uint64_t total = f_load_be64(r + 16);

    int last_pct = -1;
    auto report = [&](uint64_t done) {
        if (!progress || total == 0) {
            return;
        }
        int pct = static_cast<int>(done * 100 / total);
        if (pct != last_pct) {
            last_pct = pct;
            progress(done, total);
        }
    };
    report(offset);

    data.clear();
    char buf[65536];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            data.append(buf, static_cast<size_t>(n));
            report(offset + data.size());
        } else if (n == 0) {
            break;  // 服务端发送完毕并关闭
        } else if (errno == EINTR) {
            continue;
        } else {
            close(fd);
            return -1;
        }
    }
    report(total);
    close(fd);
    return STATUS_OK;
}
