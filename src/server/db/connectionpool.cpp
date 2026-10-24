#include "connectionpool.h"
#include <muduo/base/Logging.h>
#include <vector>

ConnectionPool &ConnectionPool::instance()
{
    static ConnectionPool pool;
    return pool;
}

void ConnectionPool::init(const string &host, int port, const string &user,
                          const string &pwd, const string &db, size_t size)
{
    lock_guard<mutex> lock(_mtx);
    _host = host;
    _port = port;
    _user = user;
    _pwd = pwd;
    _db = db;
    if (size > 0)
    {
        _maxSize = size;
    }
}

// 创建连接，调用方需持有互斥锁
MYSQL *ConnectionPool::createConn()
{
    MYSQL *conn = mysql_init(nullptr);
    if (conn == nullptr)
    {
        LOG_ERROR << "mysql_init failed!";
        return nullptr;
    }

    if (mysql_real_connect(conn, _host.c_str(), _user.c_str(), _pwd.c_str(),
                           _db.c_str(), _port, nullptr, 0) == nullptr)
    {
        LOG_ERROR << "connect mysql failed: " << mysql_error(conn);
        mysql_close(conn);
        return nullptr;
    }

    // 统一使用 utf8mb4，支持中文与 emoji
    mysql_query(conn, "set names utf8mb4");

    return conn;
}

MYSQL *ConnectionPool::acquire()
{
    unique_lock<mutex> lock(_mtx);

    while (_idle.empty())
    {
        if (_created < _maxSize)
        {
            MYSQL *conn = createConn();
            if (conn == nullptr)
            {
                return nullptr;
            }
            ++_created;
            return conn;
        }

        _cv.wait(lock);
    }

    MYSQL *conn = _idle.front();
    _idle.pop();
    return conn;
}

void ConnectionPool::release(MYSQL *conn)
{
    if (conn == nullptr)
    {
        return;
    }

    if (mysql_ping(conn) != 0)
    {
        // 连接已失效，直接销毁并释放一个名额
        mysql_close(conn);
        {
            lock_guard<mutex> lock(_mtx);
            --_created;
        }
        _cv.notify_one();
        return;
    }

    {
        lock_guard<mutex> lock(_mtx);
        _idle.push(conn);
    }
    _cv.notify_one();
}

ConnectionPool::~ConnectionPool()
{
    lock_guard<mutex> lock(_mtx);
    while (!_idle.empty())
    {
        mysql_close(_idle.front());
        _idle.pop();
    }
}
