#include <iostream>

#include "httplib.h"

constexpr int PORT = 9000;

int main() {
    httplib::Server svr;

    // 只托管 ./static 下的静态资源，禁止把整个工作目录（含二进制与数据目录）暴露给 HTTP
    svr.set_mount_point("/", "./static");

    std::cout << "HTTP server hosting ./static on port " << PORT << std::endl;
    std::cout << "Open http://localhost:" << PORT << "/index.html" << std::endl;

    svr.listen("0.0.0.0", PORT);

    return 0;
}
