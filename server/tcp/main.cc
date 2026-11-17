#include <muduo/net/EventLoop.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>

#include "server/app/Config.h"
#include "server/app/Log.h"
#include "server/app/ServiceRegistry.h"
#include "server/tcp/FrameDispatcher.h"
#include "server/tcp/TcpServerApp.h"

namespace {

eat::app::Config g_config;
muduo::net::EventLoop* g_loop = nullptr;

// Only an async-signal-safe wakeup is issued here; the real shutdown sequence
// runs on the loop thread after loop() returns, so Stop() never runs inside a
// signal handler.
void OnSignal(int) {
    if (g_loop != nullptr) {
        g_loop->quit();
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
    g_loop = &loop;

    eat::tcp::FrameDispatcher dispatcher;
    eat::app::ServiceRegistry registry(g_config, dispatcher);
    if (!registry.Init(error)) {
        eat::app::Log::Write(eat::app::LogLevel::Error, "main", "service_init_failed:" + error);
        return 4;
    }
    registry.RegisterHandlers();

    eat::tcp::TcpServerApp app(&loop, g_config, &dispatcher);
    if (!app.Start(error)) {
        eat::app::Log::Write(eat::app::LogLevel::Error, "main", "server_start_failed:" + error);
        return 3;
    }

    std::signal(SIGTERM, OnSignal);
    std::signal(SIGINT, OnSignal);

    eat::app::Log::Write(eat::app::LogLevel::Info, "main",
                         "eat-server started on port " + std::to_string(g_config.tcp_port));
    loop.loop();
    app.Stop();
    g_loop = nullptr;
    return 0;
}