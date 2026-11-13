// file.cc — 独立文件传输服务（单 Reactor + epoll + ET + 非阻塞 IO）
//   协议：文件头 + 文件名 + 二进制数据；状态机处理上传/下载/断点续传
//   上传写临时文件 + .meta 保存进度，完成 rename 成正式文件；下载用 sendfile

#include <arpa/inet.h>
#include <endian.h>
#include <fcntl.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "net/TcpServer.h"

constexpr int PORT = 8001;
constexpr char MAGIC[4] = {'F', 'I', 'L', 'E'};
constexpr uint8_t VERSION = 1;

constexpr uint8_t CMD_UPLOAD = 1;
constexpr uint8_t CMD_DOWNLOAD = 2;

constexpr uint8_t STATUS_OK = 0;
constexpr uint8_t STATUS_NOT_FOUND = 1;
constexpr uint8_t STATUS_BAD_REQ = 2;
constexpr uint8_t STATUS_IO = 3;

constexpr size_t HEADER_SIZE = 28;
constexpr size_t RESP_SIZE = 24;
constexpr uint32_t MAX_FILENAME_LEN = 255;
constexpr int MAX_EVENTS = 1024;
constexpr size_t BUF_SIZE = 65536;
constexpr const char* STORAGE_DIR = "./files";
constexpr uint64_t SENDFILE_CHUNK = 1ULL << 30;  // 单次 sendfile 上限 1GiB

volatile sig_atomic_t g_stop = 0;

// ── 字节序（网络序 / 大端） ──
void store_be32(char* p, uint32_t v) {
    uint32_t n = htobe32(v);
    std::memcpy(p, &n, 4);
}
uint32_t load_be32(const char* p) {
    uint32_t n;
    std::memcpy(&n, p, 4);
    return be32toh(n);
}
void store_be64(char* p, uint64_t v) {
    uint64_t n = htobe64(v);
    std::memcpy(p, &n, 8);
}
uint64_t load_be64(const char* p) {
    uint64_t n;
    std::memcpy(&n, p, 8);
    return be64toh(n);
}

// ── 协议结构 ──
#pragma pack(push, 1)
struct FileHeader {
    char magic[4];
    uint8_t cmd;
    uint8_t flags;
    uint8_t reserved;
    uint8_t version;
    uint32_t filename_len;
    uint64_t file_size;
    uint64_t offset;
};
struct RespHeader {
    char magic[4];
    uint8_t cmd;
    uint8_t status;
    uint16_t reserved;
    uint64_t offset;
    uint64_t total_size;
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 28, "FileHeader size must be 28");
static_assert(sizeof(RespHeader) == 24, "RespHeader size must be 24");

void unpack_header(const char* in, FileHeader& h) {
    std::memcpy(h.magic, in, 4);
    h.cmd = static_cast<uint8_t>(in[4]);
    h.flags = static_cast<uint8_t>(in[5]);
    h.reserved = static_cast<uint8_t>(in[6]);
    h.version = static_cast<uint8_t>(in[7]);
    h.filename_len = load_be32(in + 8);
    h.file_size = load_be64(in + 12);
    h.offset = load_be64(in + 20);
}

void pack_resp(uint8_t cmd, uint8_t status, uint64_t offset, uint64_t total_size,
               char* out) {
    std::memcpy(out, MAGIC, 4);
    out[4] = static_cast<char>(cmd);
    out[5] = static_cast<char>(status);
    out[6] = 0;
    out[7] = 0;
    store_be64(out + 8, offset);
    store_be64(out + 16, total_size);
}

bool valid_header(const FileHeader& h) {
    return std::memcmp(h.magic, MAGIC, 4) == 0 && h.version == VERSION &&
           (h.cmd == CMD_UPLOAD || h.cmd == CMD_DOWNLOAD) &&
           h.filename_len > 0 && h.filename_len <= MAX_FILENAME_LEN;
}

// ── 路径 ──
std::string sanitize_name(const std::string& raw) {
    if (raw.empty() || raw.find('\0') != std::string::npos) {
        return "";
    }
    size_t pos = raw.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? raw : raw.substr(pos + 1);
    if (name.empty() || name == "." || name == "..") {
        return "";
    }
    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return "";
    }
    return name;
}

std::string base_path(const std::string& name) {
    return std::string(STORAGE_DIR) + "/" + name;
}
std::string tmp_path(const std::string& name) {
    return base_path(name) + ".tmp";
}
std::string meta_path(const std::string& name) {
    return base_path(name) + ".meta";
}

uint64_t read_meta(const std::string& name) {
    int fd = open(meta_path(name).c_str(), O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(strtoull(buf, nullptr, 10));
}

bool write_meta(const std::string& name, uint64_t offset) {
    int fd = open(meta_path(name).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    std::string s = std::to_string(offset);
    ssize_t n = write(fd, s.data(), s.size());
    close(fd);
    return n == static_cast<ssize_t>(s.size());
}

void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// ── 连接状态机 ──
enum class State : uint8_t {
    READ_HEADER,
    READ_FILENAME,
    UPLOAD_STREAM,
    DOWNLOAD_STREAM,
    CLOSING,
};

class FileConn {
   public:
    FileConn(int fd, int epfd) : fd_(fd), epfd_(epfd) {}

    ~FileConn() {
        if (tmp_fd_ >= 0) {
            write_meta(name_, written_);  // 断开时才落盘进度
            close(tmp_fd_);
        }
        if (file_fd_ >= 0) {
            close(file_fd_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 返回 false 表示连接应被移除（关闭）
    bool on_readable() {
        bool eof = false;
        char buf[BUF_SIZE];
        while (true) {
            ssize_t n = recv(fd_, buf, sizeof(buf), 0);
            if (n > 0) {
                rbuf_.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                eof = true;
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
        }
        bool keep = drive();
        if (!keep) {
            return false;
        }
        if (eof) {
            return false;
        }
        return true;
    }

    bool on_writable() {
        if (!flush_out()) {
            arm_out();
            return true;
        }
        if (state_ == State::DOWNLOAD_STREAM) {
            return stream_download();
        }
        disarm_out();
        if (state_ == State::CLOSING) {
            return false;
        }
        return true;
    }

   private:
    bool drive() {
        while (true) {
            switch (state_) {
                case State::READ_HEADER: {
                    if (rbuf_.size() < HEADER_SIZE) {
                        return true;
                    }
                    FileHeader h;
                    unpack_header(rbuf_.data(), h);
                    rbuf_.erase(0, HEADER_SIZE);
                    if (!valid_header(h)) {
                        return close_with_resp(0, STATUS_BAD_REQ, 0);
                    }
                    cmd_ = h.cmd;
                    file_size_ = h.file_size;
                    offset_ = h.offset;
                    name_len_ = h.filename_len;
                    state_ = State::READ_FILENAME;
                    break;
                }
                case State::READ_FILENAME: {
                    if (rbuf_.size() < name_len_) {
                        return true;
                    }
                    std::string raw = rbuf_.substr(0, name_len_);
                    rbuf_.erase(0, name_len_);
                    name_ = sanitize_name(raw);
                    if (name_.empty()) {
                        return close_with_resp(cmd_, STATUS_BAD_REQ, 0);
                    }
                    base_ = base_path(name_);
                    bool keep = (cmd_ == CMD_UPLOAD) ? start_upload()
                                                     : start_download();
                    if (!keep) {
                        return false;
                    }
                    break;
                }
                case State::UPLOAD_STREAM:
                    if (rbuf_.empty()) {
                        return true;
                    }
                    {
                        bool keep = stream_upload();
                        rbuf_.clear();
                        if (!keep) {
                            return false;
                        }
                    }
                    return true;
                case State::DOWNLOAD_STREAM:
                    rbuf_.clear();
                    return true;
                case State::CLOSING:
                    return true;
            }
        }
    }

    bool start_upload() {
        std::string tp = tmp_path(name_);
        struct stat st;
        uint64_t resume = 0;
        if (stat(tp.c_str(), &st) == 0) {
            resume = read_meta(name_);
            if (resume > static_cast<uint64_t>(st.st_size)) {
                resume = st.st_size;
            }
            if (resume > file_size_) {
                resume = file_size_;
            }
        } else {
            unlink(meta_path(name_).c_str());  // 清理孤儿 meta
        }
        tmp_fd_ = open(tp.c_str(), O_WRONLY | O_CREAT, 0644);
        if (tmp_fd_ < 0) {
            LOG(ERROR) << "open tmp failed: " << strerror(errno);
            return close_with_resp(cmd_, STATUS_IO, 0);
        }
        ftruncate(tmp_fd_, static_cast<off_t>(resume));
        written_ = resume;
        queue_resp(CMD_UPLOAD, STATUS_OK, resume);
        if (written_ >= file_size_) {
            complete_upload();
            state_ = State::CLOSING;
            return !wbuf_.empty();
        }
        state_ = State::UPLOAD_STREAM;
        return true;
    }

    bool stream_upload() {
        uint64_t remaining = file_size_ - written_;
        size_t len = rbuf_.size();
        size_t to_write = (static_cast<uint64_t>(len) < remaining)
                              ? len
                              : static_cast<size_t>(remaining);
        size_t done = 0;
        while (done < to_write) {
            ssize_t n = pwrite(tmp_fd_, rbuf_.data() + done, to_write - done,
                               static_cast<off_t>(written_ + done));
            if (n > 0) {
                done += static_cast<size_t>(n);
            } else if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOG(ERROR) << "pwrite failed: " << strerror(errno);
                return close_with_resp(cmd_, STATUS_IO, 0);
            } else {
                return close_with_resp(cmd_, STATUS_IO, 0);
            }
        }
        written_ += done;
        if (written_ >= file_size_) {
            complete_upload();
            state_ = State::CLOSING;
            return !wbuf_.empty();
        }
        return true;
    }

    void complete_upload() {
        if (tmp_fd_ >= 0) {
            fsync(tmp_fd_);
            close(tmp_fd_);
            tmp_fd_ = -1;
        }
        std::string tp = tmp_path(name_);
        std::string bp = base_path(name_);
        std::string mp = meta_path(name_);
        if (rename(tp.c_str(), bp.c_str()) != 0) {
            LOG(ERROR) << "rename failed: " << strerror(errno);
        }
        unlink(mp.c_str());
    }

    bool start_download() {
        file_fd_ = open(base_.c_str(), O_RDONLY);
        if (file_fd_ < 0) {
            uint8_t st = (errno == ENOENT) ? STATUS_NOT_FOUND : STATUS_IO;
            return close_with_resp(cmd_, st, 0);
        }
        struct stat st;
        fstat(file_fd_, &st);
        actual_size_ = static_cast<uint64_t>(st.st_size);
        sent_ = offset_;
        if (sent_ > actual_size_) {
            sent_ = actual_size_;
        }
        queue_resp(CMD_DOWNLOAD, STATUS_OK, sent_, actual_size_);
        state_ = State::DOWNLOAD_STREAM;
        if (wbuf_.empty()) {
            return stream_download();
        }
        return true;
    }

    bool stream_download() {
        return use_copy_ ? copy_download() : sendfile_download();
    }

    bool sendfile_download() {
        while (sent_ < actual_size_) {
            off_t in_off = static_cast<off_t>(sent_);
            size_t n = static_cast<size_t>(
                std::min<uint64_t>(actual_size_ - sent_, SENDFILE_CHUNK));
            ssize_t r = sendfile(fd_, file_fd_, &in_off, n);
            if (r > 0) {
                sent_ += static_cast<uint64_t>(r);
                continue;
            }
            if (r == 0) {
                break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                arm_out();
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EINVAL || errno == ENOSYS) {
                use_copy_ = true;  // 文件系统不支持 sendfile，回退 read+send
                return copy_download();
            }
            LOG(ERROR) << "sendfile failed: " << strerror(errno);
            break;
        }
        finish_download();
        return false;
    }

    bool copy_download() {
        char buf[BUF_SIZE];
        while (sent_ < actual_size_) {
            size_t want = static_cast<size_t>(
                std::min<uint64_t>(actual_size_ - sent_, sizeof(buf)));
            ssize_t rd = pread(file_fd_, buf, want, static_cast<off_t>(sent_));
            if (rd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOG(ERROR) << "pread failed: " << strerror(errno);
                break;
            }
            if (rd == 0) {
                break;
            }
            size_t off = 0;
            while (off < static_cast<size_t>(rd)) {
                ssize_t wr = send(fd_, buf + off, static_cast<size_t>(rd) - off,
                                  MSG_NOSIGNAL);
                if (wr > 0) {
                    off += static_cast<size_t>(wr);
                } else if (wr < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        sent_ += off;
                        arm_out();
                        return true;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    LOG(ERROR) << "send failed: " << strerror(errno);
                    finish_download();
                    return false;
                } else {
                    finish_download();
                    return false;
                }
            }
            sent_ += off;
        }
        finish_download();
        return false;
    }

    void finish_download() {
        if (file_fd_ >= 0) {
            close(file_fd_);
            file_fd_ = -1;
        }
        state_ = State::CLOSING;
    }

    void queue_resp(uint8_t cmd, uint8_t status, uint64_t offset,
                    uint64_t total_size = 0) {
        char buf[RESP_SIZE];
        pack_resp(cmd, status, offset, total_size, buf);
        wbuf_.append(buf, RESP_SIZE);
        if (!flush_out()) {
            arm_out();
        }
    }

    // 入队响应并进入 CLOSING。返回 true 表示还需排空（保持连接），
    // false 表示响应已发完，可立即关闭。
    bool close_with_resp(uint8_t cmd, uint8_t status, uint64_t offset,
                         uint64_t total_size = 0) {
        queue_resp(cmd, status, offset, total_size);
        state_ = State::CLOSING;
        return !wbuf_.empty();
    }

    bool flush_out() {
        while (w_off_ < wbuf_.size()) {
            ssize_t n = send(fd_, wbuf_.data() + w_off_, wbuf_.size() - w_off_,
                             MSG_NOSIGNAL);
            if (n > 0) {
                w_off_ += static_cast<size_t>(n);
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return false;
                }
                if (errno == EINTR) {
                    continue;
                }
                return false;
            } else {
                return false;
            }
        }
        wbuf_.clear();
        w_off_ = 0;
        return true;
    }

    void arm_out() {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
        ev.data.fd = fd_;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd_, &ev);
    }

    void disarm_out() {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = fd_;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd_, &ev);
    }

    int fd_ = -1;
    int epfd_ = -1;
    State state_ = State::READ_HEADER;

    uint8_t cmd_ = 0;
    uint32_t name_len_ = 0;
    uint64_t file_size_ = 0;
    uint64_t offset_ = 0;
    std::string name_;
    std::string base_;

    std::string rbuf_;

    int tmp_fd_ = -1;
    uint64_t written_ = 0;

    int file_fd_ = -1;
    uint64_t actual_size_ = 0;
    uint64_t sent_ = 0;
    bool use_copy_ = false;

    std::string wbuf_;
    size_t w_off_ = 0;
};

// ── 文件服务（单线程事件循环） ──
class FileServer {
   public:
    FileServer() : srv_(PORT) {}

    bool start() { return srv_.start(); }

    int run() {
        epfd_ = epoll_create1(0);
        if (epfd_ < 0) {
            return -1;
        }

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = srv_.getLfd();
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, srv_.getLfd(), &ev) < 0) {
            return -1;
        }

        epoll_event events[MAX_EVENTS];
        while (!g_stop) {
            int n = epoll_wait(epfd_, events, MAX_EVENTS, -1);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                uint32_t e = events[i].events;
                if (fd == srv_.getLfd()) {
                    accept_loop();
                    continue;
                }
                auto it = conns_.find(fd);
                if (it == conns_.end()) {
                    continue;
                }
                FileConn* c = it->second.get();
                bool keep = true;
                if (e & EPOLLIN) {
                    keep = c->on_readable();
                }
                if (keep && (e & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))) {
                    keep = false;
                }
                if (keep && (e & EPOLLOUT)) {
                    keep = c->on_writable();
                }
                if (!keep) {
                    remove_conn(fd);
                }
            }
        }

        conns_.clear();
        close(epfd_);
        srv_.closeLfd();
        return 0;
    }

   private:
    void accept_loop() {
        while (true) {
            int fd = srv_.acceptCli();
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            set_nonblock(fd);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.fd = fd;
            if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == 0) {
                conns_[fd] = std::make_unique<FileConn>(fd, epfd_);
                LOG(INFO) << "new file conn fd=" << fd;
            } else {
                close(fd);
            }
        }
    }

    void remove_conn(int fd) {
        auto it = conns_.find(fd);
        if (it == conns_.end()) {
            return;
        }
        epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        conns_.erase(it);
    }

    TcpServer srv_;
    int epfd_ = -1;
    std::unordered_map<int, std::unique_ptr<FileConn>> conns_;
};

void on_signal(int) {
    g_stop = 1;
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (mkdir(STORAGE_DIR, 0755) != 0 && errno != EEXIST) {
        LOG(ERROR) << "mkdir " << STORAGE_DIR << " failed: " << strerror(errno);
    }

    FileServer srv;
    if (!srv.start()) {
        LOG(ERROR) << "file_server start failed";
        return -1;
    }
    LOG(INFO) << "file_server listening on :" << PORT << " dir=" << STORAGE_DIR;
    return srv.run();
}