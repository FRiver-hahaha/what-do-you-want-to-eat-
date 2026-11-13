#pragma once

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_map>

#include "../database/MySQLPool.h"
#include "../database/Redis.h"
#include "../email/EmailSender.h"
#include "../model/FriendModel.h"
#include "../model/GroupModel.h"
#include "../model/UserModel.h"
#include "../net/Connection.h"

using json = nlohmann::json;

class Connection;

class ChatService {
   public:
    ChatService(const ChatService&) = delete;
    ChatService& operator=(const ChatService&) = delete;

    static ChatService& instance();
    void handle(std::shared_ptr<Connection> conn, const json& js);
    void logout(std::shared_ptr<Connection> conn);

   private:
    ChatService();
    ~ChatService();

    void heartbeat(std::shared_ptr<Connection> conn, const json& js);
    void signUp(std::shared_ptr<Connection> conn, const json& js);
    void signIn(std::shared_ptr<Connection> conn, const json& js);
    void sendCode(std::shared_ptr<Connection> conn, const json& js);
    void codeLogin(std::shared_ptr<Connection> conn, const json& js);
    void changePassword(std::shared_ptr<Connection> conn, const json& js);
    void exitLogin(std::shared_ptr<Connection> conn, const json& js);
    void signOut(std::shared_ptr<Connection> conn, const json& js);
    void queryFriends(std::shared_ptr<Connection> conn, const json& js);
    void sendRequest(std::shared_ptr<Connection> conn, const json& js);
    void processRequest(std::shared_ptr<Connection> conn, const json& js);
    void blockFriend(std::shared_ptr<Connection> conn, const json& js);
    void deleteFriend(std::shared_ptr<Connection> conn, const json& js);
    void queryGroups(std::shared_ptr<Connection> conn, const json& js);
    void createGroup(std::shared_ptr<Connection> conn, const json& js);
    void inviteToGroup(std::shared_ptr<Connection> conn, const json& js);
    void applyGroups(std::shared_ptr<Connection> conn, const json& js);
    void processGroupRequest(std::shared_ptr<Connection> conn, const json& js);
    void queryMembers(std::shared_ptr<Connection> conn, const json& js);
    void setAdmin(std::shared_ptr<Connection> conn, const json& js);
    void kickMember(std::shared_ptr<Connection> conn, const json& js);
    void quitGroup(std::shared_ptr<Connection> conn, const json& js);
    void dissolveGroup(std::shared_ptr<Connection> conn, const json& js);


    std::string sha256(const std::string& input);

    // 校验并消费验证码：成功返回空串，失败返回错误提示。
    // 语义：一次性（成功即删除）、300s 过期、同一邮箱最多错 5 次。
    std::string consumeCode(const std::string& email, const std::string& code);

   private:
    using handler =
        std::function<void(std::shared_ptr<Connection>, const json&)>;
    std::unordered_map<int, handler> m_handlers;

    Redis m_redis;
    MySQLPool m_mysql_pool;
    UserModel m_user_model;
    FriendModel m_friend_model;
    GroupModel m_group_model;
    EmailSender m_email_sender;
    std::unordered_map<int, std::weak_ptr<Connection>> m_user_conn;
    std::mutex m_mutex;
    struct VerifyCode {
        std::string code;
        std::time_t expire_at{0};   // 过期时间点
        std::time_t sent_at{0};     // 最近一次发送时间点（限频用）
        int attempts{0};            // 已失败次数
    };
    std::unordered_map<std::string, VerifyCode> m_codes;
    std::mutex m_code_mutex;
    std::thread m_db_thread;
    std::deque<std::function<void()>> m_db_tasks;
    std::mutex m_db_mtx;
    std::condition_variable m_db_cv;
    std::atomic<bool> m_db_stop{false};
};