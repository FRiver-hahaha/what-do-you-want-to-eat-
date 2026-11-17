#pragma once

#include "common/common.h"

void authOptions(TcpClient& client);
void signUp(TcpClient& client);
void passwordSignIn(TcpClient& client);
void codeSignIn(TcpClient& client);