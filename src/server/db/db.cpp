#include "db.h"
#include "connectionpool.h"
#include <muduo/base/Logging.h>

// 初始化数据库连接：直接从连接池借出
MySQL::MySQL()
{
    _conn = ConnectionPool::instance().acquire();
}

// 释放数据库连接资源：归还给连接池
MySQL::~MySQL()
{
    if (_conn != nullptr)
    {
        ConnectionPool::instance().release(_conn);
        _conn = nullptr;
    }
}

// 检查借到的连接是否可用
bool MySQL::connect()
{
    return _conn != nullptr;
}

// 更新操作
bool MySQL::update(const string &sql)
{
    if (_conn == nullptr)
    {
        LOG_INFO << "mysql connection is null, update failed!";
        return false;
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "更新失败:" << mysql_error(_conn);
        return false;
    }

    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(const string &sql)
{
    if (_conn == nullptr)
    {
        LOG_INFO << "mysql connection is null, query failed!";
        return nullptr;
    }

    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "查询失败:" << mysql_error(_conn);
        return nullptr;
    }
    
    return mysql_use_result(_conn);
}

// 转义字符串，防止SQL注入与引号破坏
string MySQL::escape(const string &raw)
{
    if (_conn == nullptr)
    {
        return raw;
    }

    string escaped(raw.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(_conn, &escaped[0], raw.c_str(), raw.size());
    escaped.resize(len);
    return escaped;
}

// 获取连接
MYSQL* MySQL::getConnection()
{
    return _conn;
}
