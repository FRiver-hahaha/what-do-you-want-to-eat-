#pragma once

#include "common/common.h"

void groupPage(TcpClient& client);
void groupList(TcpClient& client);
void createGroup(TcpClient& client);
void applyGroup(TcpClient& client);
void groupMenu(TcpClient& client, const json& g);
void groupChat(TcpClient& client, const json& g);
void showGroupHistory(TcpClient& client, const json& g, int scope);
void viewGroupHistory(TcpClient& client, const json& g);