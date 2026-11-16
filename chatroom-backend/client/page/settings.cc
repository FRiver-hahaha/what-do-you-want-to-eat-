#include "settings.h"

#include <cstdlib>

#include "net/TcpClient.h"

bool settings(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("──────── 设置 ────────");
        pushOpt("======================");
        pushOpt("1. 更改密码");
        pushOpt("2. 注销账号");
        pushOpt("3. 退出登录");
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (input == "1") {
            changePassword(client);
            continue;
        } else if (input == "2") {
            if (signOut(client)) {
                return false;
            }
            continue;
        } else if (input == "3") {
            if (exitLogin(client)) {
                return false;
            }
        } else if (input == "0") {
            return true;
        }
    }
    return false;
}

void changePassword(TcpClient& client) {
    system("clear");
    pushOpt("────── 更改密码 ──────");
    pushOpt("======================");
    setPrompt("请输入新密码:");
    setPrefix("新密码:");
    g_hide_echo = true;
    std::string password = popIpt();
    json req;
    req["type"] = 3;
    req["email"] = g_user["email"];
    while (g_running) {
        pushOpt("正在发送验证码...");
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        pushOpt("\033[A\033[K\033[A");
        if (rsp["code"] != 1) {
            if (!confirm(RED + std::string(rsp["msg"]) + RESET +
                         "\n是否重新发送？")) {
                return;
            }
            pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K");
            pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K");
            pushOpt("\033[A\033[K\033[A\033[K\033[A");
        } else {
            break;
        }
    }
    while (g_running) {
        setPrompt("验证码已发送，请输入验证码:");
        setPrefix("验证码:");
        std::string code = popIpt();
        req["type"] = 5;
        req["code"] = code;
        req["password"] = password;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 1) {
            if (!confirm(RED + std::string(rsp["msg"]) + RESET +
                         "\n是否重新输入？")) {
                return;
            }
            pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K");
            pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K");
            pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K\033[A");
        } else {
            showTip(GREEN + std::string(rsp["msg"]) + RESET);
            break;
        }
    }
}

bool signOut(TcpClient& client) {
    system("clear");
    pushOpt("────── 注销账号 ──────");
    if (!confirm(YELLOW + std::string("确定要注销账号吗？此操作不可恢复！") +
                 RESET)) {
        return false;
    }
    client.sendData(R"({"type":7})");
    json rsp = popRsp();
    if (!g_running) {
        return false;
    }
    if (rsp["code"] != 1) {
        showTip(RED + std::string(rsp["msg"]) + RESET);
    } else {
        showTip(GREEN + std::string(rsp["msg"]) + RESET);
    }
    g_user = json();
    return true;
}

bool exitLogin(TcpClient& client) {
    system("clear");
    pushOpt("────── 退出登录 ──────");
    if (!confirm(YELLOW + std::string("确定要退出登录吗？") + RESET)) {
        return false;
    }
    client.sendData(R"({"type":6})");
    json rsp = popRsp();
    if (!g_running) {
        return false;
    }
    if (rsp["code"] != 1) {
        showTip(RED + std::string(rsp["msg"]) + RESET);
    } else {
        showTip(GREEN + std::string(rsp["msg"]) + RESET);
    }
    g_user = json();
    return true;
}