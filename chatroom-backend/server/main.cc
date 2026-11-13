#include <csignal>
#include <glog/logging.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/Config.h"
#include "pool.h"
#include "reactor/Reactor.h"
#include "net/TcpServer.h"
#include "net/Connection.h"
#include "service/ChatService.h"


using namespace std;

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    signal(SIGPIPE, SIG_IGN);
    // 显式构造单例: 让在线状态复位在 accept 之前完成, 而不是拖到首个请求
    ChatService::instance();
    const int sub_cnt = envInt("CHATROOM_WORKER_THREADS", 4);
    const int port = envInt("CHATROOM_PORT", 8000);
    mkdir("./files", 0755);
    pool thread_pool(sub_cnt);
    vector<unique_ptr<Reactor>> subs(sub_cnt);
    for (int i = 0; i < sub_cnt; i++) {
        subs[i] = make_unique<Reactor>();
        thread_pool.enqueue([reactor = subs[i].get()]() {
            reactor->loop();
        });
    }
    TcpServer server(port);
    if (!server.start()) {
        LOG(ERROR) << "Server start failed";
        return -1;
    }
    LOG(INFO) << "Server started on port " << port
              << " worker_threads=" << sub_cnt;
    int main_epfd = epoll_create1(0);
    epoll_event ev{}, events[1024];
    ev.events = EPOLLIN;
    ev.data.fd = server.getLfd();
    epoll_ctl(main_epfd, EPOLL_CTL_ADD, server.getLfd(), &ev);
    while (true) {
        int n = epoll_wait(main_epfd, events, 1024, -1);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == server.getLfd()) {
                while (true) {
                    int cfd = server.acceptCli();
                    if (cfd < 0) {
                        break;
                    }
                    int min_idx{0};
                    for (int j = 0; j < sub_cnt; j++) {
                        if (subs[j]->getConnCnt() <
                            subs[min_idx]->getConnCnt()) {
                            min_idx = j;
                        }
                    }
                    subs[min_idx]->pushFd(cfd);
                }
            }
        }
    }
    server.closeLfd();
    close(main_epfd);
    return 0;
}