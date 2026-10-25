#include "chatserver.hpp"
#include "chatservice.hpp"
#include "connectionpool.h"
#include "serverconfig.hpp"
#include <iostream>
#include <signal.h>
using namespace std;

// 处理服务器ctrl+c或kill结束后，重置user的状态信息
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 加载运行配置（数据库/Redis凭据外置，禁止硬编码在代码中）
    if (!ServerConfig::instance().loadDefault())
    {
        cerr << "missing config file: config/server.conf (db_host/db_user/db_password/... )" << endl;
        exit(-1);
    }
    const ServerConfig &cfg = ServerConfig::instance();

    signal(SIGINT, resetHandler);
    // SIGTERM 同样触发状态重置，避免脚本式 kill 后用户状态残留为 online
    signal(SIGTERM, resetHandler);

    // 初始化MySQL连接池
    ConnectionPool::instance().init(cfg.dbHost(), cfg.dbPort(), cfg.dbUser(),
                                    cfg.dbPassword(), cfg.dbName(), cfg.dbPoolSize());

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    server.start();
    loop.loop();

    return 0;
}
