#include "net.h"

#include <readline/readline.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "TcpClient.h"

void netLoop(TcpClient* client) {
    int epfd = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = client->getFd();
    epoll_ctl(epfd, EPOLL_CTL_ADD, client->getFd(), &ev);
    auto last_send = std::chrono::steady_clock::now();
    auto last_recv = std::chrono::steady_clock::now();
    epoll_event events[4];
    while (g_running) {
        int n = epoll_wait(epfd, events, 4, 500);
        auto now = std::chrono::steady_clock::now();
        if (now - last_send >= std::chrono::seconds(20)) {
            client->sendData(R"({"type":0})");
            last_send = now;
        }
        if (now - last_recv >= std::chrono::seconds(60)) {
            pushOpt(RED + std::string("与服务器断开连接") + RESET);
            g_running = false;
            g_ipt_cv.notify_all();
            g_rsp_cv.notify_all();
            break;
        }
        client->flush();
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd != client->getFd()) {
                continue;
            }
            std::string msg = client->recvData();
            while (!msg.empty()) {
                parseOpt(msg, last_recv);
                msg = client->recvData();
            }
            if (client->isClosed()) {
                pushOpt(RED + std::string("与服务器断开连接") + RESET);
                g_running = false;
                g_ipt_cv.notify_all();
                g_rsp_cv.notify_all();
                break;
            }
        }
    }
    close(epfd);
}

// 密码输入时隐藏回显：接管 readline 默认 redisplay，g_hide_echo 为真时只清行、不回显输入
static void noEchoRedisplay() {
    if (g_hide_echo) {
        fprintf(rl_outstream, "\r\033[K");
        fflush(rl_outstream);
    } else {
        rl_redisplay();
    }
}

// 聊天指令补全：输入 / 开头时按 Tab 补全指令
static char* dupStr(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = static_cast<char*>(malloc(n));
    if (p != nullptr) {
        memcpy(p, s, n);
    }
    return p;
}

static char* chatCommandGen(const char* text, int state) {
    static const char* commands[] = {"/q",   "/long", "/short",
                                     "/put", "/get",  "/file"};
    static int idx;
    static size_t len;
    if (state == 0) {
        idx = 0;
        len = strlen(text);
    }
    while (idx < static_cast<int>(sizeof(commands) / sizeof(commands[0]))) {
        const char* name = commands[idx++];
        if (strncmp(name, text, len) == 0) {
            return dupStr(name);
        }
    }
    return nullptr;
}

static char** chatCommandCompletion(const char* text, int start, int end) {
    (void)end;
    if (start == 0 && text[0] == '/') {
        return rl_completion_matches(text, chatCommandGen);
    }
    return nullptr;
}

void ioLoop() {
    int epfd = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
    ev.data.fd = g_opt_efd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, g_opt_efd, &ev);
    rl_callback_handler_install("", on_line);
    rl_redisplay_function = noEchoRedisplay;
    rl_attempted_completion_function = chatCommandCompletion;
    epoll_event events[2];
    while (g_running) {
        int n = epoll_wait(epfd, events, 2, 100);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == g_opt_efd) {
                uint64_t val;
                read(g_opt_efd, &val, sizeof(val));
                printOpt();
            } else if (events[i].data.fd == STDIN_FILENO) {
                rl_callback_read_char();
            }
        }
    }
    rl_callback_handler_remove();
    close(epfd);
}

void on_line(char* input) {
    if (input == nullptr) {
        g_running = false;
        g_ipt_cv.notify_all();
        g_rsp_cv.notify_all();
        return;
    }
    std::string line(input);
    if (line == "/short") {
        g_is_getline = true;
        free(input);
        std::cout << "\033[A\033[K";
        return;
    }
    if (line == "/long") {
        g_is_getline = false;
        free(input);
        std::cout << "\033[A\033[K";
        return;
    }
    if (line.empty()) {
        free(input);
        fprintf(rl_outstream, "\033[A");
        return;
    }
    if (g_is_getline) {
        pushIpts(line);
        clearLines(line);
        printLines(line);
    } else {
        pushIpt(line);
        clearLines(line);
        printLine(line);
    }
    g_hide_echo = false;
    free(input);
}

void pushIpts(const std::string& str) {
    size_t start = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\n') {
            pushIpt(str.substr(start, i - start));
            start = i + 1;
        }
    }
    pushIpt(str.substr(start));
}

void clearLines(const std::string& str) {
    if (str.empty()) {
        return;
    }
    int len = 0;
    for (char c : str) {
        if (c == '\n') {
            std::cout << "\033[A\033[K";
            len = 0;
        } else {
            if (len >= 80) {
                std::cout << "\033[A\033[K";
                len = 0;
            }
            len++;
        }
    }
    if (str.back() != '\n') {
        std::cout << "\033[A\033[K";
    }
}

void printLines(const std::string& line) {
    size_t start = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\n') {
            printLine(line.substr(start, i - start));
            start = i + 1;
        }
    }
    printLine(line.substr(start));
}

void printLine(const std::string& line) {
    std::string prompt;
    std::string prefix;
    {
        std::lock_guard<std::mutex> lk(g_ui_mutex);
        prompt = g_prompt;
        prefix = g_prefix;
    }
    std::string display = g_hide_echo ? std::string(line.size(), '*') : line;
    fprintf(rl_outstream, "\033[A\033[K%s %s\n", prefix.c_str(), display.c_str());
    if (g_chat != 0) {
        fprintf(rl_outstream, "%s\n", prompt.c_str());
    }
}

void parseOpt(const std::string& msg,
              std::chrono::steady_clock::time_point& last_recv) {
    json js;
    try {
        js = json::parse(msg);
    } catch (...) {
        return;
    }
    last_recv = std::chrono::steady_clock::now();
    int type = js["type"];
    if (type == 0) {
        return;
    }
    int code = js["code"];
    if (code == 2) {
        std::string rsp;
        if (type == 9) {
            rsp = std::string(js["name"]) + std::string(js["msg"]);
        }
        if (type == 13) {
            bool is_file = js["msg_type"];
            if (g_chat % 2 == 0 && g_chat / 2 == js["sid"]) {
                if (is_file) {
                    rsp = "\033[0m" + std::string(js["name"]) + ": [文件] " +
                          std::string(js["msg"]);
                } else {
                    rsp = "\033[0m" + std::string(js["name"]) + ": " +
                          std::string(js["msg"]);
                }
            } else {
                rsp = "好友" + std::string(js["name"]) + "发来新的消息";
            }
        }
        if (type == 17) {
            rsp = std::string(js["msg"]) + std::string(js["group_name"]);
        }
        if (type == 18) {
            rsp = std::string(js["msg"]) + std::string(js["name"]);
        }
        if (type == 19) {
            rsp = std::string(js["msg"]);
        }
        if (type == 25) {
            bool is_file = js["msg_type"];
            std::string sender = js["name"];
            if (g_chat % 2 == 1 && g_chat / 2 == js["rid"]) {
                if (is_file) {
                    rsp = "\033[0m" + sender + ": [文件] " +
                          std::string(js["msg"]);
                } else {
                    rsp = "\033[0m" + sender + ": " + std::string(js["msg"]);
                }
            } else {
                rsp = "群聊" + std::string(js["group_name"]) + "发来新的消息";
            }
        }
        std::string prompt;
        {
            std::lock_guard<std::mutex> lk(g_ui_mutex);
            prompt = g_prompt;
        }
        std::string opt = std::string(CYAN) + "\033[A\033[K" + rsp +
                          std::string("\n") + RESET + prompt;
        pushOpt(opt);
        return;
    }
    if (js["type"] == 13 || js["type"] == 25) {
        if (js["code"] == 0) {
            pushOpt("\033[A\033[K\033[A\033[K\033[A");
            pushOpt(RED + std::string(js["msg"]) + RESET);
            pushOpt("======================");
            return;
        } else if (js["code"] == 1) {
            return;
        }
    }
    pushRsp(js);
}