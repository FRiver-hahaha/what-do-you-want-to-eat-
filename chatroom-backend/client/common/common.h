#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class TcpClient;
using json = nlohmann::json;

#define RED "\033[1;91m"     // 红色
#define GREEN "\033[1;92m"   // 绿色
#define YELLOW "\033[1;93m"  // 黄色
#define CYAN "\033[1;96m"    // 青色
#define RESET "\033[0m"      // 重置

// 全局状态
extern std::atomic<bool> g_running;
extern std::mutex g_opt_mtx;
extern std::queue<std::string> g_opt_que;
extern int g_opt_efd;
extern std::mutex g_ipt_mtx;
extern std::queue<std::string> g_ipt_que;
extern std::condition_variable g_ipt_cv;
extern std::mutex g_rsp_mtx;
extern std::queue<json> g_rsp_que;
extern std::condition_variable g_rsp_cv;
extern std::mutex g_ui_mutex;
extern std::string g_prompt;
extern std::string g_prefix;
extern std::atomic<bool> g_is_getline;
extern std::atomic<bool> g_hide_echo;
extern std::atomic<int> g_chat;
extern json g_user;
extern TcpClient* g_client;
extern std::string g_host;

// UI 工具
void printOpt();
void pushOpt(const std::string& smsg);
std::vector<std::string> popOpt();
void pushIpt(const std::string& smsg);
std::string popIpt();
void pushRsp(const json& js);
json popRsp();
void setPrompt(const std::string& prompt);
void setPrefix(const std::string& prefix);
void showTip(const std::string& msg);
bool confirm(const std::string& msg);