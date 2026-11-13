#pragma once

#include "common/common.h"

bool settings(TcpClient& client);
void changePassword(TcpClient& client);
bool signOut(TcpClient& client);
bool exitLogin(TcpClient& client);