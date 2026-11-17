#include <muduo/net/EventLoop.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>

#include "server/app/Config.h"
#include "server/app/Log.h"
#include "server/app/Metrics.h"
#include "server/tcp/FrameDispatcher.h"
#include "server/tcp/TcpServerApp.h"

namespace {

eat::app::Config g_config;
eat::tcp::TcpServerApp* g_app = nullptr;

void OnSignal(int) {
    if (g_app != nullptr) {
        g_app->Stop();
    }
}

std::string ConfigPath(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            return argv[i + 1];
        }
    }
    return "/etc/eat/server.toml";
}

}  // namespace

int main(int argc, char** argv) {
    std::string error;
    const std::string path = ConfigPath(argc, argv);
    if (!g_config.LoadFromFile(path, error)) {
        eat::app::Log::Write(eat::app::LogLevel::Error, "main",
                             "config_load_failed:" + error);
        return 2;
    }

    muduo::net::EventLoop loop;
    eat::tcp::FrameDispatcher dispatcher;

    eat::tcp::TcpServerApp app(&loop, g_config, &dispatcher);
    g_app = &app;
    if (!app.Start(error)) {
        eat::app::Log::Write(eat::app::LogLevel::Error, "main", "server_start_failed:" + error);
        return 3;
    }

    std::signal(SIGTERM, OnSignal);
    std::signal(SIGINT, OnSignal);

    eat::app::Log::Write(eat::app::LogLevel::Info, "main",
                         "eat-server started on port " + std::to_string(g_config.tcp_port));
    loop.loop();
    g_app = nullptr;
    return 0;
}