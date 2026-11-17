#include "friend.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "net/TcpClient.h"
#include "settings.h"

void home(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("──────── 首页 ────────");
        pushOpt("======================");
        pushOpt("1. 好友");
        pushOpt("2. 设置");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (input == "1") {
            friendPage(client);
        } else if (input == "2") {
            if (!settings(client)) {
                return;
            }
        }
    }
}

void friendPage(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("──────── 好友 ────────");
        pushOpt("======================");
        pushOpt("1. 我的好友");
        pushOpt("2. 添加好友");
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (input == "1") {
            friendList(client);
        } else if (input == "2") {
            addFriend(client);
        } else if (input == "0") {
            return;
        }
    }
}

void addFriend(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("────── 添加好友 ──────");
        pushOpt("======================");
        setPrompt("请输入对方邮箱:");
        setPrefix("对方邮箱:");
        std::string email = popIpt();
        json req;
        req["type"] = 9;
        req["email"] = email;
        req["my_email"] = g_user["email"];
        req["name"] = g_user["name"];
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] == 1) {
            pushOpt(GREEN + std::string(rsp["msg"]) + RESET);
        } else {
            pushOpt(RED + std::string(rsp["msg"]) + RESET);
        }
        if (!confirm("是否继续添加？")) {
            return;
        }
    }
}

void friendList(TcpClient& client) {
    while (g_running) {
        system("clear");
        json req;
        req["type"] = 8;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        pushOpt("────── 好友列表 ──────");
        pushOpt("======================");
        int cnt = 0;
        for (auto& f : rsp["friends"]) {
            cnt++;
            int state = f["state"];
            int status = f["status"];
            std::string relation;
            if (status == 2) {
                relation = "好友";
            } else if (status == 3) {
                relation = "已拉黑";
            } else if (status == 0) {
                relation = "待我处理";
            } else {
                relation = "待对方同意";
            }
            std::string online = (state == 1) ? "在线" : "离线";
            pushOpt(std::to_string(cnt) + ". " + std::string(f["name"]) + "-" +
                    std::string(f["email"]) + "（" + online + "） " + relation +
                    "");
        }
        if (cnt == 0) {
            pushOpt("（暂无好友）");
        }
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择好友 :");
        setPrefix("好友:");
        std::string input = popIpt();
        if (input == "0") {
            return;
        }
        int idx = atoi(input.c_str());
        if (idx >= 1 && idx <= cnt) {
            friendMenu(client, rsp["friends"][idx - 1]);
        }
    }
}

void friendMenu(TcpClient& client, const json& f) {
    int id = f["id"];
    int status = f["status"];
    while (g_running) {
        system("clear");
        pushOpt("────── 好友操作 ──────");
        pushOpt("======================");
        pushOpt(std::string(f["name"]) + "(" + std::string(f["email"]) + ")");
        pushOpt("======================");
        if (status == 2) {
            pushOpt("1. 拉黑");
            pushOpt("2. 删除");
            pushOpt("0. 返回");
        } else if (status == 3) {
            pushOpt("1. 取消拉黑");
            pushOpt("2. 删除");
            pushOpt("0. 返回");
        } else if (status == 0) {
            pushOpt("1. 同意");
            pushOpt("2. 拒绝");
            pushOpt("0. 返回");
        } else {
            pushOpt("等待对方同意");
            pushOpt("0. 返回");
        }
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        json req;
        bool send = false;
        if (status == 2) {
            if (input == "1") {
                req["type"] = 11;
                req["id"] = id;
                req["block"] = true;
                send = true;
            } else if (input == "2") {
                if (!confirm(YELLOW + std::string("确定要删除好友 ") +
                             std::string(f["name"]) + std::string(" 吗？") +
                             RESET)) {
                    continue;
                }
                req["type"] = 12;
                req["id"] = id;
                send = true;
            } else if (input == "0") {
                return;
            }
        } else if (status == 3) {
            if (input == "1") {
                req["type"] = 11;
                req["id"] = id;
                req["block"] = false;
                send = true;
            } else if (input == "2") {
                if (!confirm(YELLOW + std::string("确定要删除好友 ") +
                             std::string(f["name"]) + std::string(" 吗？") +
                             RESET)) {
                    continue;
                }
                req["type"] = 12;
                req["id"] = id;
                send = true;
            } else if (input == "0") {
                return;
            }
        } else if (status == 0) {
            if (input == "1") {
                req["type"] = 10;
                req["id"] = id;
                req["agree"] = 1;
                send = true;
            } else if (input == "2") {
                req["type"] = 10;
                req["id"] = id;
                req["agree"] = 0;
                send = true;
            } else if (input == "0") {
                return;
            }
        } else {
            if (input == "0") {
                return;
            }
        }
        if (!send) {
            continue;
        }

        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        showTip(GREEN + std::string(rsp["msg"]) + RESET);
        return;
    }
}
