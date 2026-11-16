#pragma once

#include "common/common.h"

void home(TcpClient& client);
void friendPage(TcpClient& client);
void addFriend(TcpClient& client);
void friendList(TcpClient& client);
void friendMenu(TcpClient& client, const json& f);
