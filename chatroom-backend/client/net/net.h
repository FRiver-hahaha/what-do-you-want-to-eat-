#pragma once

#include <chrono>

#include "common/common.h"

void netLoop(TcpClient* client);
void ioLoop();
void on_line(char* line);
void pushIpts(const std::string& str);
void clearLines(const std::string& str);
void printLines(const std::string& line);
void printLine(const std::string& line);
void parseOpt(const std::string& msg,
              std::chrono::steady_clock::time_point& last_recv);