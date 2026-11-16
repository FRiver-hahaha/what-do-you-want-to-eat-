#include <mysql/mysql.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// 最小 SQL 文件执行器：仅用于在受限环境验证项目自身的迁移/回滚脚本。
// mysql CLI 的 DELIMITER 是客户端指令，这里去掉 DELIMITER 行与 $$ 终止符后
// 交给服务端多语句解析（BEGIN...END 块由服务端正确识别）。
int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <sql-file>\n", argv[0]);
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::printf("open_failed %s\n", argv[1]);
        return 2;
    }
    std::stringstream raw;
    raw << input.rdbuf();

    std::string cleaned;
    std::istringstream lines(raw.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("DELIMITER", 0) == 0) {
            continue;
        }
        std::size_t pos = line.find("$$");
        while (pos != std::string::npos) {
            line.replace(pos, 2, ";");
            pos = line.find("$$");
        }
        cleaned += line;
        cleaned += '\n';
    }

    MYSQL* conn = mysql_init(nullptr);
    unsigned int timeout = 3;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    if (mysql_real_connect(conn, nullptr, "root", "", "chatroom_p8", 3306,
                           "/run/mysqld/mysqld.sock", CLIENT_MULTI_STATEMENTS) ==
        nullptr) {
        std::printf("connect_failed %s\n", mysql_error(conn));
        mysql_close(conn);
        return 3;
    }

    if (mysql_query(conn, cleaned.c_str()) != 0) {
        std::printf("SQL_ERROR %u: %s\n", mysql_errno(conn), mysql_error(conn));
        mysql_close(conn);
        return 4;
    }

    int status = 0;
    do {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)) != nullptr) {
                for (unsigned int i = 0; i < mysql_num_fields(result); ++i) {
                    if (i != 0) {
                        std::printf("\t");
                    }
                    std::printf("%s", row[i] == nullptr ? "NULL" : row[i]);
                }
                std::printf("\n");
            }
            mysql_free_result(result);
        }
        status = mysql_next_result(conn);
    } while (status == 0);

    if (status > 0) {
        std::printf("SQL_ERROR(after results) %u: %s\n", mysql_errno(conn),
                    mysql_error(conn));
        mysql_close(conn);
        return 4;
    }

    mysql_close(conn);
    std::printf("EXEC_OK\n");
    return 0;
}