#pragma once

#include "common/common.h"

void home(TcpClient& client);
void friendPage(TcpClient& client);
void addFriend(TcpClient& client);
void friendList(TcpClient& client);
void friendMenu(TcpClient& client, const json& f);
void onechat(TcpClient& client, const json& f);
void showHistory(TcpClient& client, const json& f, int scope);
void viewHistory(TcpClient& client, const json& f);
void uploadFile(TcpClient& client, const std::string& arg);
void downloadFile(TcpClient& client, const std::string& ref);
void pendingFiles(TcpClient& client);