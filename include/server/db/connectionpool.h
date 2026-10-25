#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <mysql/mysql.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
using namespace std;

// MySQL连接池，避免每次业务操作都重新建立连接
class ConnectionPool
{
public:
    // 获取连接池单例对象
    static ConnectionPool &instance();

    // 初始化连接参数（在服务器启动时调用一次）
    void init(const string &host, int port, const string &user,
              const string &pwd, const string &db, size_t size = 8);

    // 借出一条连接，池满时阻塞等待
    MYSQL *acquire();

    // 归还连接，连接失效时销毁
    void release(MYSQL *conn);

    ~ConnectionPool();

private:
    ConnectionPool() = default;

    // 创建一条新连接
    MYSQL *createConn();

    string _host = "127.0.0.1";
    int _port = 3306;
    string _user = "root";
    string _pwd; // 凭据由配置文件提供，禁止硬编码
    string _db = "chat";
    size_t _maxSize = 8;
    size_t _created = 0;

    queue<MYSQL *> _idle;
    mutex _mtx;
    condition_variable _cv;
};

#endif
