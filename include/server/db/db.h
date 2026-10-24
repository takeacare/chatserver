#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>
using namespace std;

// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    // 检查借到的连接是否可用
    bool connect();
    // 更新操作
    bool update(const string &sql);
    // 查询操作
    MYSQL_RES *query(const string &sql);
    // 转义字符串，防止SQL注入与引号破坏
    string escape(const string &raw);
    // 获取连接
    MYSQL* getConnection();
private:
    MYSQL *_conn;
};

#endif