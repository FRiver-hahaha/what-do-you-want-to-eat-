#include <iostream>

#include "httplib.h"

constexpr int PORT = 9000;

int main() {
    httplib::Server svr;

    svr.set_mount_point("/", ".");

    std::cout << "HTTP server hosting index.html on port " << PORT << std::endl;
    std::cout << "Open http://localhost:" << PORT << "/index.html" << std::endl;

    svr.listen("0.0.0.0", PORT);

    return 0;
}
