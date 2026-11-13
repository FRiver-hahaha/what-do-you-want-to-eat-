#include "friend.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "group.h"
#include "net/TcpClient.h"
#include "net/filetransfer.h"
#include "settings.h"

void home(TcpClient& client) {
    while (g_running) {
        system("clear");
        pushOpt("──────── 首页 ────────");
        pushOpt("======================");
        pushOpt("1. 好友");
        pushOpt("2. 群聊");
        pushOpt("3. 设置");
        pushOpt("======================");
        setPrompt("请选择:");
        setPrefix("选择:");
        std::string input = popIpt();
        if (input == "1") {
            friendPage(client);
        } else if (input == "2") {
            groupPage(client);
        } else if (input == "3") {
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
            pushOpt("1. 私聊");
            pushOpt("2. 查看聊天记录");
            pushOpt("3. 拉黑");
            pushOpt("4. 删除");
            pushOpt("0. 返回");
        } else if (status == 3) {
            pushOpt("1. 私聊");
            pushOpt("2. 查看聊天记录");
            pushOpt("3. 取消拉黑");
            pushOpt("4. 删除");
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
                onechat(client, f);
                continue;
            } else if (input == "2") {
                viewHistory(client, f);
                continue;
            } else if (input == "3") {
                req["type"] = 11;
                req["id"] = id;
                req["block"] = true;
                send = true;
            } else if (input == "4") {
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
                onechat(client, f);
                continue;
            } else if (input == "2") {
                viewHistory(client, f);
                continue;
            } else if (input == "3") {
                req["type"] = 11;
                req["id"] = id;
                req["block"] = false;
                send = true;
            } else if (input == "4") {
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

void onechat(TcpClient& client, const json& f) {
    int id = f["id"];
    std::string name = f["name"];
    g_chat = id * 2;
    system("clear");
    pushOpt("私聊: " + name);
    pushOpt("（输入消息，/q 退出）");
    pushOpt("======================");
    showHistory(client, f, 0);
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
        if (input == "/file") {
            pendingFiles(client);
            continue;
        }
        json req;
        req["type"] = 13;
        req["rid"] = id;
        req["msg"] = input;
        req["msg_type"] = false;
        client.sendData(req.dump());
    }
    g_chat = 0;
}

void showHistory(TcpClient& client, const json& f, int scope) {
    int id = f["id"];
    std::string name = f["name"];
    int my_id = g_user["id"];
    json req;
    req["type"] = 14;
    req["id"] = id;
    req["scope"] = scope;
    client.sendData(req.dump());
    json rsp = popRsp();
    if (!g_running) {
        return;
    }
    if (rsp["msg"].empty()) {
        pushOpt("（暂无聊天记录）");
    }
    for (auto& m : rsp["msg"]) {
        int sender_id = m["sender_id"];
        int is_file = m["is_file"];
        std::string content = m["content"];
        if (is_file == 1) {
            content = "[文件] " + content;
        }
        std::string who = (sender_id == my_id) ? "我" : name;
        pushOpt(who + ": " + content);
    }
}

void viewHistory(TcpClient& client, const json& f) {
    system("clear");
    pushOpt("======================");
    showHistory(client, f, 1);
    pushOpt("======================");
    pushOpt("聊天记录: " + std::string(f["name"]));
    setPrompt("（输入任意内容返回）");
    setPrefix("返回:");
    popIpt();
}

// 文件传输进度：写入 stderr 用 \r 原地刷新，避免与 readline 主界面互相干扰
static void showProgress(uint64_t done, uint64_t total) {
    int pct = (total == 0) ? 100 : static_cast<int>(done * 100 / total);
    fprintf(stderr, "\r\033[K(%llu/%llu) %d%%",
            static_cast<unsigned long long>(done),
            static_cast<unsigned long long>(total), pct);
    fflush(stderr);
}

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) {
        return "";
    }
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

void uploadFile(TcpClient& client, const std::string& arg) {
    int type = (g_chat % 2 == 0) ? 13 : 25;
    int rid = g_chat / 2;
    // 解析：续传形式为 "<ref> <路径>"，否则整个当作本地路径；容忍多余空格
    std::string s = trim(arg);
    std::string ref;
    std::string path = s;
    size_t sp = s.find_first_of(" \t");
    if (sp != std::string::npos && f_is_ref(s.substr(0, sp))) {
        ref = s.substr(0, sp);
        path = trim(s.substr(sp + 1));
    }
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        pushOpt("\033[A\033[K\033[A");
        pushOpt(RED + std::string("文件不存在: ") + path + RESET);
        pushOpt("======================");
        return;
    }
    ifs.close();
    std::string name = path.substr(path.find_last_of('/') + 1);
    if (ref.empty()) {
        json req;
        req["type"] = type;
        req["rid"] = rid;
        req["msg"] = name;
        req["msg_type"] = true;
        client.sendData(req.dump());
        json rsp = popRsp();
        if (!g_running) {
            return;
        }
        if (rsp["code"] != 4) {
            return;
        }
        ref = std::string(rsp["msg"]);
    }
    pushOpt("\033[A\033[K\033[A\033[K\033[A");
    pushOpt("我: [文件] " + ref);
    int status = f_upload(g_host, ref, path, showProgress);
    fprintf(stderr, "\n");
    pushOpt("\033[A\033[K\033[A");
    if (status == 0) {
        pushOpt(GREEN + std::string("文件已发送") + RESET);
    } else {
        pushOpt(RED + std::string("文件上传失败，续传: /put ") + ref + " " +
                path + RESET);
    }
    pushOpt("======================");
}

void downloadFile(TcpClient& client, const std::string& ref) {
    std::string r = trim(ref);
    std::string name = r;
    size_t pos = r.find('_');
    if (pos != std::string::npos) {
        name = r.substr(pos + 1);
    }
    // 拉取当前会话的文件列表，校验 ref 是否属于本会话、是否已上传完成
    int rid = g_chat / 2;
    bool is_group = (g_chat % 2 == 1);
    json req;
    req["type"] = 27;
    req["rid"] = rid;
    req["is_group"] = is_group;
    client.sendData(req.dump());
    json rsp = popRsp();
    if (!g_running) {
        return;
    }
    bool found = false;
    bool pending = false;
    if (rsp["code"] == 1) {
        for (auto& f : rsp["msg"]) {
            if (std::string(f["ref"]) == r) {
                found = true;
                pending = f["upload_pending"].get<bool>();
                break;
            }
        }
    }
    if (!found) {
        pushOpt("\033[A\033[K\033[A\033[K\033[A");
        pushOpt(RED + std::string("文件不存在") + RESET);
        pushOpt("======================");
        return;
    }
    if (pending) {
        pushOpt("\033[A\033[K\033[A\033[K\033[A");
        pushOpt(RED + std::string("文件未上传完成") + RESET);
        pushOpt("======================");
        return;
    }
    std::string part = "./downloads/" + name + ".part";
    std::string final = "./downloads/" + name;
    uint64_t offset = 0;
    struct stat st;
    if (stat(part.c_str(), &st) == 0) {
        offset = static_cast<uint64_t>(st.st_size);
    }
    pushOpt("\033[A\033[K\033[A\033[K\033[A\033[K\033[A");
    pushOpt(GREEN "正在下载文件: " + r + RESET);
    std::string data;
    int status = f_download(g_host, r, offset, data, showProgress);
    fprintf(stderr, "\n");
    pushOpt("\033[A\033[K\033[A");
    if (status == 1) {
        pushOpt(RED + std::string("文件不存在") + RESET);
    } else {
        // 追加本次收到的字节（含中断时已收到的部分），供下次 /get 续传
        std::ofstream ofs(part, std::ios::binary | std::ios::app);
        ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        ofs.close();
        if (status == 0) {
            if (std::rename(part.c_str(), final.c_str()) != 0) {
                pushOpt(RED + std::string("文件保存失败") + RESET);
            } else {
                pushOpt(GREEN + std::string("文件已保存为: downloads/") + name +
                        RESET);
            }
        } else {
            pushOpt(RED + std::string("文件下载中断，重新 /get 续传") + RESET);
        }
    }
    pushOpt("======================");
}

void pendingFiles(TcpClient& client) {
    int rid = g_chat / 2;
    bool is_group = (g_chat % 2 == 1);
    json req;
    req["type"] = 27;
    req["rid"] = rid;
    req["is_group"] = is_group;
    client.sendData(req.dump());
    json rsp = popRsp();
    if (!g_running) {
        return;
    }
    pushOpt("\033[A\033[K\033[A\033[K\033[A");
    if (rsp["code"] != 1) {
        pushOpt(RED + std::string(rsp["msg"]) + RESET);
        pushOpt("======================");
        return;
    }
    if ((rsp["msg"]).empty()) {
        pushOpt(GREEN "当前聊天暂无文件" RESET);
        pushOpt("======================");
        return;
    }
    std::vector<json> pending_upload;
    std::vector<json> pending_download;
    std::vector<json> available;
    for (auto& f : rsp["msg"]) {
        std::string name = f["name"];
        std::string ref = f["ref"];
        if (f["upload_pending"].get<bool>()) {
            pending_upload.push_back(f);
        } else {
            available.push_back(f);
        }
        std::string part = "./downloads/" + name + ".part";
        struct stat st;
        if (stat(part.c_str(), &st) == 0) {
            pending_download.push_back(f);
        }
    }
    if (!pending_upload.empty()) {
        pushOpt(YELLOW "上传未完成: " RESET);
        for (auto& f : pending_upload) {
            std::string name = f["name"];
            std::string ref = f["ref"];
            if (f["upload_pending"].get<bool>()) {
                pushOpt(YELLOW + name + std::string("  →  /put ") + ref +
                        " <路径>" + RESET);
            }
        }
    }
    if (!pending_download.empty()) {
        pushOpt(YELLOW "下载未完成: " RESET);
        for (auto& f : pending_download) {
            std::string name = f["name"];
            std::string ref = f["ref"];
            std::string part = "./downloads/" + name + ".part";
            struct stat st;
            if (stat(part.c_str(), &st) == 0) {
                pushOpt(YELLOW + name + std::string("  →  /get ") + ref +
                        RESET);
            }
        }
    }
    if (!available.empty()) {
        pushOpt(GREEN "可下载文件: " RESET);
        for (auto& f : available) {
            std::string name = f["name"];
            std::string ref = f["ref"];
            if (!f["upload_pending"].get<bool>()) {
                pushOpt(GREEN + ref + RESET);
            }
        }
    }
    pushOpt("======================");
}