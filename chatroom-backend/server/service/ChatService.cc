#include <glog/logging.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

#include "../common/Config.h"

#include "ChatService.h"


namespace {

// 统一的字段取值入口：缺失返回默认值，类型不符不抛异常。
// 原实现用 js["key"]（const operator[]），字段缺失直接 assert 打死进程。
std::string jsonStr(const json& js, const char* key,
                    const std::string& def = "") {
    auto it = js.find(key);
    if (it == js.end() || it->is_null()) {
        return def;
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? "1" : "0";
    }
    return def;
}

int jsonInt(const json& js, const char* key, int def = 0) {
    auto it = js.find(key);
    if (it == js.end() || it->is_null()) {
        return def;
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    if (it->is_number_integer()) {
        return it->get<int>();
    }
    if (it->is_string()) {
        try {
            return std::stoi(it->get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

bool jsonBool(const json& js, const char* key, bool def = false) {
    auto it = js.find(key);
    if (it == js.end() || it->is_null()) {
        return def;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number_integer()) {
        return it->get<int>() != 0;
    }
    if (it->is_string()) {
        std::string v = it->get<std::string>();
        return v == "1" || v == "true";
    }
    return def;
}

// 验证码策略：默认 5 分钟有效、60 秒内不可重发、最多错 5 次。
// 允许用环境变量覆盖，便于自动化测试压缩时间窗。
int codeTtlSeconds() { return envInt("CHATROOM_CODE_TTL", 300); }
int codeResendSeconds() { return envInt("CHATROOM_CODE_RESEND", 60); }
int codeMaxAttempts() { return envInt("CHATROOM_CODE_MAX_ATTEMPTS", 5); }

}  // namespace

ChatService& ChatService::instance() {
    static ChatService service;
    return service;
}

void ChatService::handle(std::shared_ptr<Connection> conn, const json& js) {
    int type = jsonInt(js, "type", -1);
    try {
        auto it = m_handlers.find(type);
        if (it != m_handlers.end()) {
            it->second(conn, js);
        } else {
            conn->getReactor()->handleWrite(
                conn, R"({"type":0,"code":0,"msg":"异常请求"})");
            LOG(ERROR) << "Invalid request type=" << type;
        }
    } catch (const std::exception& e) {
        // 任何未预料的字段类型/格式问题都只影响本次请求，不拖垮进程
        LOG(ERROR) << "handle type=" << type << " failed: " << e.what();
        json resp;
        resp["type"] = type > 0 ? type : 0;
        resp["code"] = 0;
        resp["msg"] = "请求参数缺失或格式错误";
        conn->getReactor()->handleWrite(conn, resp.dump());
    }
}

void ChatService::logout(std::shared_ptr<Connection> conn) {
    int id = conn->getUserId();
    if (id == -1) {
        return;
    }
    m_user_model.updateState(id, 0);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_user_conn.erase(id);
}

ChatService::ChatService()
    : m_user_model(&m_mysql_pool),
      m_friend_model(&m_mysql_pool),
      m_group_model(&m_mysql_pool) {
    // 所有连接参数来自环境变量，源码不留明文凭据（见 .env.example）
    const std::string mysql_host = envOr("CHATROOM_MYSQL_HOST", "127.0.0.1");
    const int mysql_port = envInt("CHATROOM_MYSQL_PORT", 3306);
    const std::string mysql_user = envOr("CHATROOM_MYSQL_USER", "chatserver");
    const std::string mysql_pass = envOr("CHATROOM_MYSQL_PASSWORD", "");
    const std::string mysql_db = envOr("CHATROOM_MYSQL_DB", "chatroom");

    if (!m_mysql_pool.init(8, mysql_user, mysql_pass, mysql_db, mysql_host,
                           static_cast<unsigned short>(mysql_port))) {
        LOG(FATAL) << "MySQL pool init failed, host=" << mysql_host
                   << " port=" << mysql_port << " user=" << mysql_user
                   << " db=" << mysql_db;
    }

    const std::string redis_host = envOr("CHATROOM_REDIS_HOST", "127.0.0.1");
    const int redis_port = envInt("CHATROOM_REDIS_PORT", 6379);
    const std::string redis_pass = envOr("CHATROOM_REDIS_PASSWORD", "");

    if (!m_redis.init(8, redis_host, static_cast<unsigned short>(redis_port),
                      redis_pass)) {
        LOG(FATAL) << "Redis pool init failed, host=" << redis_host
                   << " port=" << redis_port;
    }
    // 上次进程异常退出会残留 state=1, 导致该账号永远"已在其他设备登录"
    m_user_model.resetState();
    // m_handlers初始化业务函数分发
    m_handlers[0] = [this](std::shared_ptr<Connection> c, const json& j) {
        heartbeat(c, j);
    };
    m_handlers[1] = [this](std::shared_ptr<Connection> c, const json& j) {
        signUp(c, j);
    };
    m_handlers[2] = [this](std::shared_ptr<Connection> c, const json& j) {
        signIn(c, j);
    };
    m_handlers[3] = [this](std::shared_ptr<Connection> c, const json& j) {
        sendCode(c, j);
    };
    m_handlers[4] = [this](std::shared_ptr<Connection> c, const json& j) {
        codeLogin(c, j);
    };
    m_handlers[5] = [this](std::shared_ptr<Connection> c, const json& j) {
        changePassword(c, j);
    };
    m_handlers[6] = [this](std::shared_ptr<Connection> c, const json& j) {
        exitLogin(c, j);
    };
    m_handlers[7] = [this](std::shared_ptr<Connection> c, const json& j) {
        signOut(c, j);
    };
    m_handlers[8] = [this](std::shared_ptr<Connection> c, const json& j) {
        queryFriends(c, j);
    };
    m_handlers[9] = [this](std::shared_ptr<Connection> c, const json& j) {
        sendRequest(c, j);
    };
    m_handlers[10] = [this](std::shared_ptr<Connection> c, const json& j) {
        processRequest(c, j);
    };
    m_handlers[11] = [this](std::shared_ptr<Connection> c, const json& j) {
        blockFriend(c, j);
    };
    m_handlers[12] = [this](std::shared_ptr<Connection> c, const json& j) {
        deleteFriend(c, j);
    };
    m_handlers[15] = [this](std::shared_ptr<Connection> c, const json& j) {
        queryGroups(c, j);
    };
    m_handlers[16] = [this](std::shared_ptr<Connection> c, const json& j) {
        createGroup(c, j);
    };
    m_handlers[17] = [this](std::shared_ptr<Connection> c, const json& j) {
        inviteToGroup(c, j);
    };
    m_handlers[18] = [this](std::shared_ptr<Connection> c, const json& j) {
        applyGroups(c, j);
    };
    m_handlers[19] = [this](std::shared_ptr<Connection> c, const json& j) {
        processGroupRequest(c, j);
    };
    m_handlers[20] = [this](std::shared_ptr<Connection> c, const json& j) {
        queryMembers(c, j);
    };
    m_handlers[21] = [this](std::shared_ptr<Connection> c, const json& j) {
        setAdmin(c, j);
    };
    m_handlers[22] = [this](std::shared_ptr<Connection> c, const json& j) {
        kickMember(c, j);
    };
    m_handlers[23] = [this](std::shared_ptr<Connection> c, const json& j) {
        quitGroup(c, j);
    };
    m_handlers[24] = [this](std::shared_ptr<Connection> c, const json& j) {
        dissolveGroup(c, j);
    };
    // 任务队列取出调用
    m_db_thread = std::thread([this] {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_db_mtx);
                m_db_cv.wait(
                    lk, [this] { return !m_db_tasks.empty() || m_db_stop; });
                if (m_db_tasks.empty()) {
                    return;
                }
                task = std::move(m_db_tasks.front());
                m_db_tasks.pop_front();
            }
            task();
        }
    });
}

ChatService::~ChatService() {
    {
        std::lock_guard<std::mutex> lk(m_db_mtx);
        m_db_stop = true;
    }
    m_db_cv.notify_all();
    if (m_db_thread.joinable()) {
        m_db_thread.join();
    }
}

void ChatService::heartbeat(std::shared_ptr<Connection> conn, const json& js) {
    conn->getReactor()->handleWrite(conn, R"({"type":0})");
}

void ChatService::signUp(std::shared_ptr<Connection> conn, const json& js) {
    std::string email = jsonStr(js, "email");
    std::string name = jsonStr(js, "name");
    std::string password = jsonStr(js, "password");
    std::string code = jsonStr(js, "code");

    if (email.empty() || name.empty() || password.empty()) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":1,"code":0,"msg":"邮箱、昵称或密码不能为空"})");
        return;
    }
    if (code.empty()) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":1,"code":0,"msg":"验证码不能为空"})");
        return;
    }
    User user;
    if (m_user_model.queryByEmail(email, user)) {
        // 先判重再校验，避免已注册邮箱白白消耗一次验证码
        conn->getReactor()->handleWrite(
            conn, R"({"type":1,"code":0,"msg":"该邮箱已存在"})");
        return;
    }
    std::string code_err = consumeCode(email, code);
    if (!code_err.empty()) {
        json resp;
        resp["type"] = 1;
        resp["code"] = 0;
        resp["msg"] = code_err;
        conn->getReactor()->handleWrite(conn, resp.dump());
        return;
    }
    user.setName(name);
    user.setEmail(email);
    user.setPassword(sha256(password));
    user.setState(0);
    if (m_user_model.insert(user)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":1,"code":1,"msg":"用户信息创建成功"})");
    } else {
        conn->getReactor()->handleWrite(
            conn, R"({"type":1,"code":0,"msg":"用户信息创建失败"})");
    }
}

void ChatService::signIn(std::shared_ptr<Connection> conn, const json& js) {
    std::string email = jsonStr(js, "email");
    User user;
    if (!m_user_model.queryByEmail(email, user)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":2,"code":0,"msg":"该账号不存在"})");
        return;
    }
    if (user.getPassword() != sha256(jsonStr(js, "password"))) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":2,"code":0,"msg":"密码错误"})");
        return;
    }
    // 原子抢占登录位：仅当 state 由 0 变为 1 成功时才算登录成功。
    // 原实现是「先查 state 再更新」，并发下多个连接可同时登录同一账号。
    if (!m_user_model.tryLogin(user.getId())) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":2,"code":0,"msg":"该账号已在其他设备登录"})");
        return;
    }
    conn->setUserId(user.getId());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_user_conn[user.getId()] = conn;
    }
    json response;
    response["type"] = 2;
    response["code"] = 1;
    response["msg"] = "登录成功";
    response["id"] = user.getId();
    response["name"] = user.getName();
    response["email"] = user.getEmail();
    json offline = json::array();
    std::string data;
    auto pop_offline = [&](const std::string& key) {
        while (m_redis.rpop(key, data)) {
            try {
                offline.push_back(json::parse(data));
            } catch (...) {
            }
        }
    };
    pop_offline("offline_request:" + std::to_string(user.getId()));
    pop_offline("offline_group_request:" + std::to_string(user.getId()));
    response["offline"] = offline;
    conn->getReactor()->handleWrite(conn, response.dump());
    LOG(INFO) << "SignIn success uid=" << user.getId()
              << " email=" << user.getEmail();
}

void ChatService::sendCode(std::shared_ptr<Connection> conn, const json& js) {
    std::string email = jsonStr(js, "email");
    if (email.empty()) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":3,"code":0,"msg":"邮箱不能为空"})");
        return;
    }
    // 发送频率限制：同一邮箱 60 秒内只能发一次，防止刷邮件/撞库
    {
        std::lock_guard<std::mutex> lock(m_code_mutex);
        auto it = m_codes.find(email);
        if (it != m_codes.end()) {
            std::time_t now = std::time(nullptr);
            long elapsed = static_cast<long>(now - it->second.sent_at);
            if (elapsed >= 0 && elapsed < codeResendSeconds()) {
                int wait = codeResendSeconds() - static_cast<int>(elapsed);
                json resp;
                resp["type"] = 3;
                resp["code"] = 0;
                resp["msg"] = "发送过于频繁，请 " + std::to_string(wait) +
                              " 秒后重试";
                conn->getReactor()->handleWrite(conn, resp.dump());
                return;
            }
        }
    }

    std::string code = []() -> std::string {
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 999999);
        std::ostringstream oss;
        oss << std::setw(6) << std::setfill('0') << dist(gen);
        return oss.str();
    }();
    {
        std::lock_guard<std::mutex> lock(m_code_mutex);
        VerifyCode vc;
        vc.code = code;
        vc.sent_at = std::time(nullptr);
        vc.expire_at = vc.sent_at + codeTtlSeconds();
        vc.attempts = 0;
        m_codes[email] = vc;
    }
    if (!m_email_sender.sendCode(email, code)) {
        std::lock_guard<std::mutex> lock(m_code_mutex);
        m_codes.erase(email);
        conn->getReactor()->handleWrite(
            conn, R"({"type":3,"code":0,"msg":"邮件发送失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":3,"code":1,"msg":"验证码已发送"})");
}

void ChatService::codeLogin(std::shared_ptr<Connection> conn, const json& js) {
    std::string email = jsonStr(js, "email");
    std::string code = jsonStr(js, "code");
    if (email.empty() || code.empty()) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":4,"code":0,"msg":"邮箱或验证码不能为空"})");
        return;
    }
    {
        std::string code_err = consumeCode(email, code);
        if (!code_err.empty()) {
            json resp;
            resp["type"] = 4;
            resp["code"] = 0;
            resp["msg"] = code_err;
            conn->getReactor()->handleWrite(conn, resp.dump());
            return;
        }
    }
    User user;
    if (m_user_model.queryByEmail(email, user)) {
        if (!m_user_model.tryLogin(user.getId())) {
            conn->getReactor()->handleWrite(
                conn, R"({"type":4,"code":0,"msg":"该账号已在其他设备登录"})");
            return;
        }
        conn->setUserId(user.getId());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_user_conn[user.getId()] = conn;
        }
        json response;
        response["type"] = 4;
        response["code"] = 1;
        response["msg"] = "登录成功";
        response["id"] = user.getId();
        response["name"] = user.getName();
        response["email"] = user.getEmail();
        json offline = json::array();
        std::string data;
        auto pop_offline = [&](const std::string& key) {
            while (m_redis.rpop(key, data)) {
                try {
                    offline.push_back(json::parse(data));
                } catch (...) {
                }
            }
        };
        pop_offline("offline_request:" + std::to_string(user.getId()));
        pop_offline("offline_group_request:" + std::to_string(user.getId()));
                response["offline"] = offline;
        conn->getReactor()->handleWrite(conn, response.dump());
        LOG(INFO) << "CodeLogin success uid=" << user.getId()
                  << " email=" << email;
    } else {
        conn->getReactor()->handleWrite(
            conn, R"({"type":4,"code":0,"msg":"该账号不存在"})");
    }
}

void ChatService::changePassword(std::shared_ptr<Connection> conn,
                                 const json& js) {
    std::string email = jsonStr(js, "email");
    std::string code = jsonStr(js, "code");
    std::string password = jsonStr(js, "password");
    if (email.empty() || code.empty() || password.empty()) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":5,"code":0,"msg":"邮箱、验证码或密码不能为空"})");
        return;
    }
    {
        std::string code_err = consumeCode(email, code);
        if (!code_err.empty()) {
            json resp;
            resp["type"] = 5;
            resp["code"] = 0;
            resp["msg"] = code_err;
            conn->getReactor()->handleWrite(conn, resp.dump());
            return;
        }
    }
    User user;
    if (!m_user_model.queryByEmail(email, user)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":5,"code":0,"msg":"邮箱未注册"})");
        return;
    }
    if (!m_user_model.updatePassword(user.getId(), sha256(password))) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":5,"code":0,"msg":"密码修改失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":5,"code":1,"msg":"密码已修改"})");
}

void ChatService::exitLogin(std::shared_ptr<Connection> conn, const json& js) {
    int user_id = conn->getUserId();
    if (user_id == -1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":6,"code":0,"msg":"未登录"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":6,"code":1,"msg":"已退出登录"})");
    LOG(INFO) << "ExitLogin uid=" << user_id;
    logout(conn);
    conn->setUserId(-1);
}

void ChatService::signOut(std::shared_ptr<Connection> conn, const json& js) {
    int user_id = conn->getUserId();
    if (user_id == -1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":7,"code":0,"msg":"未登录"})");
        return;
    }
    m_friend_model.removeAll(user_id);
    m_group_model.removeFromAll(user_id);
    m_user_model.remove(user_id);
    conn->getReactor()->handleWrite(conn,
                                    R"({"type":7,"code":1,"msg":"已注销"})");
    LOG(INFO) << "SignOut uid=" << user_id;
    logout(conn);
    conn->setUserId(-1);
}

void ChatService::queryFriends(std::shared_ptr<Connection> conn,
                               const json& js) {
    int user_id = conn->getUserId();
    std::vector<User> friends = m_friend_model.queryFriends(conn->getUserId());
    json response;
    response["type"] = 8;
    response["code"] = 1;
    response["msg"] = "成功获取到好友列表";
    for (auto& user : friends) {
        json friend_info;
        friend_info["id"] = user.getId();
        friend_info["name"] = user.getName();
        friend_info["email"] = user.getEmail();
        friend_info["state"] = user.getState();
        friend_info["status"] = m_friend_model.isFriend(user_id, user.getId());
        response["friends"].push_back(friend_info);
    }
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::sendRequest(std::shared_ptr<Connection> conn,
                              const json& js) {
    int user_id = conn->getUserId();
    std::string email = jsonStr(js, "email");
    std::string my_email = jsonStr(js, "my_email");
    std::string name = jsonStr(js, "name");
    User user;
    if (!m_user_model.queryByEmail(email, user)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":0,"msg":"该用户不存在"})");
        return;
    }
    int friend_id = user.getId();
    if (friend_id == user_id) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":0,"msg":"不能添加自己为好友"})");
        return;
    }
    if (m_friend_model.isFriend(user_id, friend_id) != -1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":0,"msg":"请勿重复添加"})");
        return;
    }
    if (!m_friend_model.insert(user_id, friend_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":0,"msg":"好友申请发送失败"})");
        return;
    }
    json response;
    response["type"] = 9;
    response["code"] = 2;
    response["msg"] = "发来好友申请";
    response["id"] = user_id;
    response["name"] = name;
    response["email"] = my_email;
    std::shared_ptr<Connection> friend_conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_user_conn.find(friend_id);
        if (it != m_user_conn.end()) {
            friend_conn = it->second.lock();
        }
    }
    if (friend_conn) {
        friend_conn->getReactor()->post([friend_conn, data = response.dump()] {
            friend_conn->sendData(data + '\n');
        });
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":1,"msg":"成功发送申请"})");
        return;
    }
    if (!m_redis.lpush("offline_request:" + std::to_string(friend_id),
                       response.dump())) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":9,"code":0,"msg":"好友申请发送失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":9,"code":1,"msg":"成功发送申请"})");
}

void ChatService::processRequest(std::shared_ptr<Connection> conn,
                                 const json& js) {
    int user_id = conn->getUserId();
    int agree = jsonInt(js, "agree");
    int friend_id = jsonInt(js, "id");
    User user;
    if (!m_user_model.queryById(friend_id, user)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":10,"code":0,"msg":"该用户已注销"})");
        return;
    }
    if (agree == 1) {
        m_friend_model.addFriend(user_id, friend_id);
        conn->getReactor()->handleWrite(
            conn, R"({"type":10,"code":1,"msg":"成功添加为好友"})");
        return;
    }
    m_friend_model.deleteFriend(user_id, friend_id);
    conn->getReactor()->handleWrite(
        conn, R"({"type":10,"code":1,"msg":"已拒绝该申请"})");
}

void ChatService::blockFriend(std::shared_ptr<Connection> conn,
                              const json& js) {
    int user_id = conn->getUserId();
    int friend_id = jsonInt(js, "id");
    bool block = jsonBool(js, "block");
    int status = m_friend_model.isFriend(user_id, friend_id);
    if (status != 2 && status != 3) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":11,"code":0,"msg":"操作失败"})");
        return;
    }
    int new_status = block ? 3 : 2;
    if (!m_friend_model.updateStatus(user_id, friend_id, new_status)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":11,"code":0,"msg":"操作失败"})");
        return;
    }
    std::string msg = block ? "已拉黑" : "已取消拉黑";
    json response;
    response["type"] = 11;
    response["code"] = 1;
    response["msg"] = msg;
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::deleteFriend(std::shared_ptr<Connection> conn,
                               const json& js) {
    int user_id = conn->getUserId();
    int friend_id = jsonInt(js, "id");
    if (!m_friend_model.deleteFriend(user_id, friend_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":12,"code":0,"msg":"删除好友失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":12,"code":1,"msg":"成功删除该好友"})");
}



void ChatService::queryGroups(std::shared_ptr<Connection> conn,
                              const json& js) {
    std::vector<json> groups = m_group_model.queryGroups(conn->getUserId());
    json response;
    response["type"] = 15;
    response["code"] = 1;
    response["msg"] = groups;
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::createGroup(std::shared_ptr<Connection> conn,
                              const json& js) {
    std::string name = jsonStr(js, "name");
    int user_id = conn->getUserId();
    int group_id = 0;
    if (!m_group_model.createGroup(name, user_id, group_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":16,"code":0,"msg":"创建群聊失败"})");
        return;
    }
    json response;
    response["type"] = 16;
    response["code"] = 1;
    response["msg"] = "成功创建群聊";
    response["group_id"] = group_id;
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::inviteToGroup(std::shared_ptr<Connection> conn,
                                const json& js) {
    int group_id = jsonInt(js, "group_id");
    int friend_id = jsonInt(js, "id");
    int user_id = conn->getUserId();
    int user_role = m_group_model.getRole(group_id, user_id);
    if (user_role < 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":17,"code":0,"msg":"无权限"})");
        return;
    }
    if (m_friend_model.isFriend(user_id, friend_id) < 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":17,"code":0,"msg":"不是好友"})");
        return;
    }
    if (m_group_model.isInGroup(group_id, friend_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":17,"code":0,"msg":"对方已在群中"})");
        return;
    }
    if (!m_group_model.addMember(group_id, friend_id, 1)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":17,"code":0,"msg":"邀请失败"})");
        return;
    }
    json notify;
    notify["type"] = 17;
    notify["code"] = 2;
    notify["group_id"] = group_id;
    notify["group_name"] = m_group_model.getGroupName(group_id);
    notify["msg"] = "你已被邀请加入群聊";
    std::shared_ptr<Connection> friend_conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_user_conn.find(friend_id);
        if (it != m_user_conn.end()) {
            friend_conn = it->second.lock();
        }
    }
    if (friend_conn) {
        friend_conn->getReactor()->post([friend_conn, data = notify.dump()] {
            friend_conn->sendData(data + '\n');
        });
    } else {
        m_redis.lpush("offline_msg:" + std::to_string(friend_id),
                      notify.dump());
    }
    conn->getReactor()->handleWrite(conn,
                                    R"({"type":17,"code":1,"msg":"邀请成功"})");
}

void ChatService::applyGroups(std::shared_ptr<Connection> conn,
                              const json& js) {
    int group_id = jsonInt(js, "group_id");
    std::string email = jsonStr(js, "email");
    std::string name = jsonStr(js, "name");
    int user_id = conn->getUserId();
    if (!m_group_model.groupExist(group_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":18,"code":0,"msg":"该群聊不存在"})");
        return;
    }
    if (m_group_model.isInGroup(group_id, user_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":18,"code":0,"msg":"禁止重复加入该群"})");
        return;
    }
    std::vector<int> users;
    if (!m_group_model.applyGroup(group_id, user_id, users)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":18,"code":0,"msg":"入群申请发送失败"})");
        return;
    }
    json response;
    response["type"] = 18;
    response["code"] = 2;
    response["msg"] = "有新的入群申请";
    response["id"] = user_id;
    response["name"] = name;
    response["email"] = email;
    for (int& user : users) {
        std::shared_ptr<Connection> user_conn;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_user_conn.find(user);
            if (it != m_user_conn.end()) {
                user_conn = it->second.lock();
            }
        }
        if (user_conn) {
            user_conn->getReactor()->post([user_conn, data = response.dump()] {
                user_conn->sendData(data + '\n');
            });
        } else {
            if (!m_redis.lpush("offline_group_request:" + std::to_string(user),
                               response.dump())) {
                conn->getReactor()->handleWrite(
                    conn, R"({"type":18,"code":0,"msg":"入群申请发送失败"})");
                return;
            }
        }
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":18,"code":1,"msg":"入群申请发送成功"})");
}

void ChatService::processGroupRequest(std::shared_ptr<Connection> conn,
                                      const json& js) {
    int group_id = jsonInt(js, "group_id");
    bool agree = jsonBool(js, "agree");
    int applicant_id = jsonInt(js, "id");
    int user_id = conn->getUserId();
    int user_role = m_group_model.getRole(group_id, user_id);
    if (user_role < 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":19,"code":0,"msg":"无权限"})");
        return;
    }
    int applicant_role = m_group_model.getRole(group_id, applicant_id);
    if (applicant_role != 0) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":19,"code":0,"msg":"该申请已被处理"})");
        return;
    }
    if (!m_group_model.processGroupRequest(group_id, applicant_id, agree)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":19,"code":0,"msg":"操作失败"})");
        return;
    }
    json notify;
    notify["type"] = 19;
    notify["code"] = 2;
    notify["group_id"] = group_id;
    notify["agree"] = agree;
    notify["msg"] = agree ? "管理员同意入群申请" : "管理员拒绝入群申请";
    std::shared_ptr<Connection> user_conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_user_conn.find(applicant_id);
        if (it != m_user_conn.end()) {
            user_conn = it->second.lock();
        }
    }
    if (user_conn) {
        user_conn->getReactor()->post([user_conn, data = notify.dump()] {
            user_conn->sendData(data + '\n');
        });
    } else {
        m_redis.lpush("offline_msg:" + std::to_string(applicant_id),
                      notify.dump());
    }
    conn->getReactor()->handleWrite(conn,
                                    R"({"type":19,"code":1,"msg":"操作成功"})");
}

void ChatService::queryMembers(std::shared_ptr<Connection> conn,
                               const json& js) {
    int group_id = jsonInt(js, "group_id");
    int user_id = conn->getUserId();
    if (m_group_model.getRole(group_id, user_id) <= 0) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":20,"code":0,"msg":"不在该群中"})");
        return;
    }
    auto members = m_group_model.queryMembers(group_id);
    json response;
    response["type"] = 20;
    response["code"] = 1;
    for (auto& [member_id, role] : members) {
        json info;
        User user;
        m_user_model.queryById(member_id, user);
        info["id"] = user.getId();
        info["name"] = user.getName();
        info["email"] = user.getEmail();
        info["role"] = role;
        response["members"].push_back(info);
    }
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::setAdmin(std::shared_ptr<Connection> conn, const json& js) {
    int group_id = jsonInt(js, "group_id");
    int target_id = jsonInt(js, "id");
    bool admin = jsonBool(js, "admin");
    int user_id = conn->getUserId();
    if (m_group_model.getRole(group_id, user_id) != 3) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":21,"code":0,"msg":"仅群主可操作"})");
        return;
    }
    int target_role = m_group_model.getRole(group_id, target_id);
    if (admin && target_role != 1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":21,"code":0,"msg":"目标成员状态异常"})");
        return;
    }
    if (!admin && target_role != 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":21,"code":0,"msg":"目标并非管理员"})");
        return;
    }
    int new_role = admin ? 2 : 1;
    if (!m_group_model.setRole(group_id, target_id, new_role)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":21,"code":0,"msg":"操作失败"})");
        return;
    }
    std::string msg = admin ? "成功设为管理员" : "已撤销管理员";
    json response;
    response["type"] = 21;
    response["code"] = 1;
    response["msg"] = msg;
    conn->getReactor()->handleWrite(conn, response.dump());
}

void ChatService::kickMember(std::shared_ptr<Connection> conn, const json& js) {
    int group_id = jsonInt(js, "group_id");
    int target_id = jsonInt(js, "id");
    int user_id = conn->getUserId();
    if (target_id == user_id) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"不能踢自己"})");
        return;
    }
    int user_role = m_group_model.getRole(group_id, user_id);
    int target_role = m_group_model.getRole(group_id, target_id);
    if (target_role == -1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"目标不在群中"})");
        return;
    }
    if (target_role == 3) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"不能踢群主"})");
        return;
    }
    if (user_role == 2 && target_role >= 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"管理员不能踢管理员"})");
        return;
    }
    if (user_role < 2) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"无权限"})");
        return;
    }
    if (!m_group_model.delMember(group_id, target_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":22,"code":0,"msg":"操作失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":22,"code":1,"msg":"已踢出成员"})");
}

void ChatService::quitGroup(std::shared_ptr<Connection> conn, const json& js) {
    int group_id = jsonInt(js, "group_id");
    int user_id = conn->getUserId();
    int role = m_group_model.getRole(group_id, user_id);
    if (role == -1) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":23,"code":0,"msg":"不在该群中"})");
        return;
    }
    if (role == 3) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":23,"code":0,"msg":"群主不能直接退出"})");
        return;
    }
    if (!m_group_model.delMember(group_id, user_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":23,"code":0,"msg":"退出失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":23,"code":1,"msg":"已退出群聊"})");
}

void ChatService::dissolveGroup(std::shared_ptr<Connection> conn,
                                const json& js) {
    int group_id = jsonInt(js, "group_id");
    int user_id = conn->getUserId();
    if (m_group_model.getRole(group_id, user_id) != 3) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":24,"code":0,"msg":"仅群主可解散群聊"})");
        return;
    }
    if (!m_group_model.dissolveGroup(group_id)) {
        conn->getReactor()->handleWrite(
            conn, R"({"type":24,"code":0,"msg":"解散失败"})");
        return;
    }
    conn->getReactor()->handleWrite(
        conn, R"({"type":24,"code":1,"msg":"已解散群聊"})");
}





std::string ChatService::consumeCode(const std::string& email,
                                    const std::string& code) {
    std::lock_guard<std::mutex> lock(m_code_mutex);
    auto it = m_codes.find(email);
    if (it == m_codes.end()) {
        return "请先获取验证码";
    }

    std::time_t now = std::time(nullptr);
    if (now > it->second.expire_at) {
        m_codes.erase(it);
        return "验证码已过期，请重新获取";
    }
    if (it->second.code != code) {
        if (++it->second.attempts >= codeMaxAttempts()) {
            m_codes.erase(it);
            return "验证码错误次数过多，请重新获取";
        }
        return "验证码错误";
    }
    m_codes.erase(it);  // 一次性：校验通过立即作废，防重放
    return "";
}

std::string ChatService::sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(),
           hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}