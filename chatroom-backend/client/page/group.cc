#include "group.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

#include "friend.h"
#include "net/TcpClient.h"
#include "net/filetransfer.h"

void groupPage(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("──────── 群聊 ────────");
        pushOpt("======================");
        pushOpt("1. 我的群聊");
        pushOpt("2. 创建群聊");
        pushOpt("3. 申请入群");
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (input == "1") {
            groupList(client);
        } else if (input == "2") {
            createGroup(client);
        } else if (input == "3") {
            applyGroup(client);
        } else if (input == "0") {
            return;
        }
    }
}

void groupList(TcpClient& client) {
    while (g_running) {
        system("clear");
        json req;
        req["type"] = 15;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        pushOpt("────── 我的群聊 ──────");
        pushOpt("======================");
        int cnt = 0;
        for (auto& g : rsp["msg"]) {
            cnt++;
            pushOpt(std::to_string(cnt) + ". " + std::string(g["name"]));
        }
        if (cnt == 0) {
            pushOpt("（暂无群聊）");
        }
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择群聊:");
        setPrefix("群聊:");
        std::string input = popIpt();
        if (input == "0") {
            return;
        }
        int idx = atoi(input.c_str());
        if (idx >= 1 && idx <= cnt) {
            groupMenu(client, rsp["msg"][idx - 1]);
        }
    }
}

void createGroup(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("────── 创建群聊 ──────");
        pushOpt("======================");
        setPrompt("请输入群名称:");
        setPrefix("群名称:");
        std::string name = popIpt();
        if (name.empty()) {
            continue;
        }
        if (!confirm("确定创建群聊吗")) {
            return;
        }
        json req;
        req["type"] = 16;
        req["name"] = name;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 1) {
            showTip(RED + std::string(rsp["msg"]) + RESET);
        } else {
            showTip(GREEN + std::string(rsp["msg"]) + RESET);
        }
        return;
    }
}

void applyGroup(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("────── 申请入群 ──────");
        pushOpt("======================");
        setPrompt("请输入群 id:");
        setPrefix("群id:");
        std::string gid = popIpt();
        if (gid.empty()) {
            continue;
        }
        json req;
        req["type"] = 18;
        req["group_id"] = atoi(gid.c_str());
        req["email"] = g_user["email"];
        req["name"] = g_user["name"];
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 1) {
            showTip(RED + std::string(rsp["msg"]) + RESET);
        } else {
            showTip(GREEN + std::string(rsp["msg"]) + RESET);
        }
        return;
    }
}

static void inviteToGroup(TcpClient& client, int group_id) {
    json freq;
    freq["type"] = 8;
    client.sendData(freq.dump());
    json frsp = popRsp();
    if (!g_running) {
        return;
    }
    std::vector<int> friend_ids;
    system("clear");
    pushOpt("──── 邀请好友入群 ────");
    pushOpt("======================");
    int cnt = 0;
    for (auto& f : frsp["friends"]) {
        if (f["status"] >= 2) {
            cnt++;
            friend_ids.push_back(f["id"]);
            pushOpt(std::to_string(cnt) + ". " + std::string(f["name"]));
        }
    }
    if (cnt == 0) {
        pushOpt("（暂无好友可邀请）");
    }
    pushOpt("0. 返回");
    pushOpt("======================");
    setPrompt("请选择好友:");
    setPrefix("好友:");
    std::string sel = popIpt();
    if (sel == "0") {
        return;
    }
    int idx = atoi(sel.c_str());
    if (idx < 1 || idx > cnt) {
        showTip(RED + std::string("不存在该好友") + RESET);
        return;
    }
    json req;
    req["type"] = 17;
    req["group_id"] = group_id;
    req["id"] = friend_ids[idx - 1];
    client.sendData(req.dump());
    json rsp = popRsp();
    if (!g_running) {
        return;
    }
    if (rsp["code"] != 1) {
        showTip(RED + std::string(rsp["msg"]) + RESET);
    } else {
        showTip(GREEN + std::string(rsp["msg"]) + RESET);
    }
}

static void memberOps(TcpClient& client, int group_id) {
    while (g_running) {
        json req;
        req["type"] = 20;
        req["group_id"] = group_id;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 1) {
            showTip(RED + std::string(rsp["msg"]) + RESET);
            return;
        }
        system("clear");
        pushOpt("───── 群成员操作 ─────");
        pushOpt("======================");
        int cnt = 0;
        for (auto& m : rsp["members"]) {
            cnt++;
            int role = m["role"];
            std::string role_name = (role == 3)   ? "群主"
                                    : (role == 2) ? "管理员"
                                    : (role == 1) ? "成员"
                                                  : "待审核";
            pushOpt(std::to_string(cnt) + ". " + std::string(m["name"]) + "-" +
                    std::string(m["email"]) + role_name);
        }
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择成员:");
        setPrefix("成员:");
        std::string input = popIpt();
        if (input == "0") {
            return;
        }
        int idx = atoi(input.c_str());
        if (idx < 1 || idx > cnt) {
            continue;
        }
        json target = rsp["members"][idx - 1];
        int target_role = target["role"];
        int target_id = target["id"];
        std::string target_name = target["name"];
        if (target_role == 3) {
            showTip(RED + std::string("不可操作群主") + RESET);
            continue;
        }
        system("clear");
        pushOpt("────── 操作成员 ──────");
        pushOpt("======================");
        pushOpt(target_name);
        pushOpt("======================");
        if (target_role == 0) {
            pushOpt("1. 同意");
            pushOpt("2. 拒绝");
            pushOpt("0. 返回");
            pushOpt("======================");
            setPrompt("请选择:");
            setPrefix("选择:");
            std::string choice = popIpt();
            if (!g_running) {
                return;
            }
            if (choice != "1" && choice != "2") {
                continue;
            }
            req["type"] = 19;
            req["group_id"] = group_id;
            req["id"] = target_id;
            req["agree"] = (choice == "1");
            client.sendData(req.dump());
            json rsp2 = popRsp();
            if (!g_running) {
                return;
            }
            if (rsp2["code"] != 1) {
                showTip(RED + std::string(rsp2["msg"]) + RESET);
            } else {
                showTip(GREEN + std::string(rsp2["msg"]) + RESET);
            }
            continue;
        }
        pushOpt("1. 踢出群聊");
        if (target_role == 1) {
            pushOpt("2. 设置管理员");
        } else {
            pushOpt("2. 撤销管理员");
        }
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string choice = popIpt();
        if (!g_running) {
            return;
        }
        if (choice == "1") {
            if (!confirm(YELLOW + std::string("确定要踢出 ") + target_name +
                         std::string(" 吗？") + RESET)) {
                continue;
            }
            req["type"] = 22;
            req["group_id"] = group_id;
            req["id"] = target_id;
            client.sendData(req.dump());
            json rsp2 = popRsp();
            if (!g_running) {
                return;
            }
            if (rsp2["code"] != 1) {
                showTip(RED + std::string(rsp2["msg"]) + RESET);
            } else {
                showTip(GREEN + std::string(rsp2["msg"]) + RESET);
            }
            continue;
        } else if (choice == "2") {
            req["type"] = 21;
            req["group_id"] = group_id;
            req["id"] = target_id;
            req["admin"] = (target_role == 1);
            client.sendData(req.dump());
            json rsp2 = popRsp();
            if (!g_running) {
                return;
            }
            if (rsp2["code"] != 1) {
                showTip(RED + std::string(rsp2["msg"]) + RESET);
            } else {
                showTip(GREEN + std::string(rsp2["msg"]) + RESET);
            }
            continue;
        }
        continue;
    }
}

void groupMenu(TcpClient& client, const json& g) {
    int group_id = g["id"];
    std::string group_name = g["name"];
    int my_id = g_user["id"];
    while (g_running) {
        json req;
        req["type"] = 20;
        req["group_id"] = group_id;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 1) {
            showTip(RED + std::string(rsp["msg"]) + RESET);
            return;
        }
        int my_role = -1;
        for (auto& m : rsp["members"]) {
            if (m["id"] == my_id) {
                my_role = m["role"];
            }
        }
        system("clear");
        pushOpt("─────── 群操作 ───────");
        pushOpt("======================");
        pushOpt(group_name + " (id:" + std::to_string(group_id) + ")");
        pushOpt("======================");
        pushOpt("1. 进入群聊");
        pushOpt("2. 查看聊天记录");
        pushOpt("3. 邀请好友进群");
        pushOpt("4. 群成员操作");
        pushOpt("5. 解散群");
        pushOpt("0. 返回");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (!g_running) {
            return;
        }
        if (input == "1") {
            groupChat(client, g);
            continue;
        } else if (input == "2") {
            viewGroupHistory(client, g);
            continue;
        } else if (input == "3") {
            if (my_role < 2) {
                showTip(RED + std::string("无权限") + RESET);
                continue;
            }
            inviteToGroup(client, group_id);
            continue;
        } else if (input == "4") {
            memberOps(client, group_id);
            continue;
        } else if (input == "5") {
            if (my_role != 3) {
                showTip(RED + std::string("无权限") + RESET);
                continue;
            }
            if (!confirm(YELLOW +
                         std::string("确定要解散该群吗？此操作不可恢复！") +
                         RESET)) {
                continue;
            }
            req["type"] = 24;
            req["group_id"] = group_id;
            client.sendData(req.dump());
            json rsp2 = popRsp();
            if (!g_running) {
                return;
            }
            if (rsp2["code"] != 1) {
                showTip(RED + std::string(rsp2["msg"]) + RESET);
            } else {
                showTip(GREEN + std::string(rsp2["msg"]) + RESET);
            }
            return;
        } else if (input == "0") {
            return;
        }
    }
}

void groupChat(TcpClient& client, const json& g) {
    int group_id = g["id"];
    std::string group_name = g["name"];
    g_chat = group_id * 2 + 1;
    system("clear");
    pushOpt("群聊: " + group_name);
    pushOpt("（输入消息，/q 退出）");
    pushOpt("======================");
    showGroupHistory(client, g, 0);
    setPrompt("======================");
    setPrefix("我:");
    while (g_running) {
        std::string input = popIpt();
        if (input.size() > 65535) {
            pushOpt("\033[A\033[K");
            pushOpt(RED "消息内容过长，无法发送" RESET);
            pushOpt("======================");
            continue;
        }
        if (!g_running) {
            break;
        }
        if (input == "/q") {
            break;
        }
        if (input.rfind("/put ", 0) == 0) {
            uploadFile(client, input.substr(5));
            continue;
        }
        if (input.rfind("/get ", 0) == 0) {
            downloadFile(client, input.substr(5));
            continue;
        }
        if (input == "/pending") {
            pendingFiles(client);
            continue;
        }
        json req;
        req["type"] = 25;
        req["rid"] = group_id;
        req["msg"] = input;
        req["msg_type"] = false;
        client.sendData(req.dump());
    }
    g_chat = 0;
}

void showGroupHistory(TcpClient& client, const json& g, int scope) {
    int group_id = g["id"];
    int my_id = g_user["id"];
    json req;
    req["type"] = 26;
    req["group_id"] = group_id;
    req["scope"] = scope;
    client.sendData(req.dump());
    json rsp = popRsp();
    if (!g_running) {
        return;
    }
    if (rsp["code"] != 1) {
        showTip(RED + std::string(rsp["msg"]) + RESET);
        return;
    }
    if (rsp["msg"].empty()) {
        pushOpt("（暂无聊天记录）");
    }
    for (auto& m : rsp["msg"]) {
        int sender_id = m["sender_id"];
        int is_file = m["is_file"];
        std::string content = m["content"];
        std::string sender_name = m["sender_name"];
        if (is_file == 1) {
            content = "[文件] " + content;
        }
        std::string who = (sender_id == my_id) ? "我" : sender_name;
        pushOpt(who + ": " + content);
    }
}

void viewGroupHistory(TcpClient& client, const json& g) {
    system("clear");
    pushOpt("======================");
    showGroupHistory(client, g, 1);
    pushOpt("======================");
    pushOpt("聊天记录: " + std::string(g["name"]));
    setPrompt("（输入任意内容返回）");
    setPrefix("返回:");
    popIpt();
}