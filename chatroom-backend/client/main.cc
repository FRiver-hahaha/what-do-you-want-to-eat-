#include <signal.h>
#include <sys/eventfd.h>
#include <sys/stat.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "common/common.h"
#include "net/TcpClient.h"
#include "net/net.h"
#include "page/auth.h"
#include "pool.h"

using namespace std;

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    mkdir("./downloads", 0755);
    string host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? stoi(argv[2]) : 8000;
    g_host = host;
    TcpClient client;
    g_client = &client;
    if (!client.connectServer(host, port)) {
        cerr << RED << "连接失败" << RESET << endl;
        return 1;
    }
    g_opt_efd = eventfd(0, EFD_NONBLOCK);

    pool pool(4);
    pool.enqueue(netLoop, &client);
    pool.enqueue(ioLoop);
    authOptions(client);
    return 0;
}