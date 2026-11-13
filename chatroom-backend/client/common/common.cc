#include "common.h"

#include <readline/readline.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

std::atomic<bool> g_running{true};

std::mutex g_opt_mtx;
std::queue<std::string> g_opt_que;
int g_opt_efd;

std::mutex g_ipt_mtx;
std::queue<std::string> g_ipt_que;
std::condition_variable g_ipt_cv;

std::mutex g_rsp_mtx;
std::queue<json> g_rsp_que;
std::condition_variable g_rsp_cv;

std::mutex g_ui_mutex;
std::string g_prompt;
std::string g_prefix;

std::atomic<bool> g_is_getline = true;
std::atomic<bool> g_hide_echo = false;
std::atomic<int> g_chat = 0;

json g_user;
TcpClient* g_client = nullptr;
std::string g_host;

void printOpt() {
    auto msgs = popOpt();
    if (msgs.empty()) {
        return;
    }
    int saved_point = rl_point;
    char* saved_line = rl_copy_text(0, rl_end);
    fprintf(rl_outstream, "\r\033[K");
    for (auto& m : msgs) {
        fprintf(rl_outstream, "%s", m.c_str());
    }
    rl_on_new_line();
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free(saved_line);
    fflush(rl_outstream);
}

void pushOpt(const std::string& smsg) {
    {
        std::lock_guard<std::mutex> lk(g_opt_mtx);
        g_opt_que.push(smsg + "\n");
    }
    uint64_t one = 1;
    write(g_opt_efd, &one, sizeof(one));
}

std::vector<std::string> popOpt() {
    std::lock_guard<std::mutex> lk(g_opt_mtx);
    std::vector<std::string> msgs;
    while (!g_opt_que.empty()) {
        msgs.push_back(g_opt_que.front());
        g_opt_que.pop();
    }
    return msgs;
}

void pushIpt(const std::string& smsg) {
    {
        std::lock_guard<std::mutex> lk(g_ipt_mtx);
        g_ipt_que.push(smsg);
    }
    g_ipt_cv.notify_one();
}

std::string popIpt() {
    std::unique_lock<std::mutex> lk(g_ipt_mtx);
    g_ipt_cv.wait(lk, [] { return !g_ipt_que.empty() || !g_running; });
    if (!g_running || g_ipt_que.empty()) {
        return "";
    }
    std::string s = g_ipt_que.front();
    g_ipt_que.pop();
    return s;
}

void pushRsp(const json& js) {
    {
        std::lock_guard<std::mutex> lk(g_rsp_mtx);
        g_rsp_que.push(js);
    }
    g_rsp_cv.notify_one();
}

json popRsp() {
    std::unique_lock<std::mutex> lk(g_rsp_mtx);
    g_rsp_cv.wait(lk, [] { return !g_rsp_que.empty() || !g_running; });
    if (!g_running || g_rsp_que.empty()) {
        return json();
    }
    json js = g_rsp_que.front();
    g_rsp_que.pop();
    return js;
}

void setPrompt(const std::string& prompt) {
    pushOpt(prompt);
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    g_prompt = prompt;
}

void setPrefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lk(g_ui_mutex);
    g_prefix = prefix;
}

void showTip(const std::string& msg) {
    system("clear");
    pushOpt(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

bool confirm(const std::string& msg) {
    pushOpt("======================");
    pushOpt(msg);
    pushOpt("1. 确认    0. 取消");
    pushOpt("======================");
    setPrompt("请选择:");
    setPrefix("选择:");
    while (g_running) {
        std::string input = popIpt();
        if (input == "1") {
            return true;
        } else if (input == "0") {
            return false;
        }
        pushOpt("\033[A\033[K请选择:");
    }
    return false;
}