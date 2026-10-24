#include "usermodel.hpp"
#include "db.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace std;

// User表的增加方法
bool UserModel::insert(User &user)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    // 1.组装sql语句（字符串字段统一转义）
    string sql = "insert into user(name, password, state) values('" +
                 mysql.escape(user.getName()) + "', '" +
                 mysql.escape(user.getPwd()) + "', '" +
                 mysql.escape(user.getState()) + "')";

    if (mysql.update(sql))
    {
        // 获取插入成功的用户数据生成的主键id
        user.setId(mysql_insert_id(mysql.getConnection()));
        return true;
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return User();
    }

    // 1.组装sql语句
    string sql = "select * from user where id = " + to_string(id);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return User();
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    User user;
    if (row != nullptr)
    {
        user.setId(atoi(row[0]));
        user.setName(row[1] ? row[1] : "");
        user.setPwd(row[2] ? row[2] : "");
        user.setState(row[3] ? row[3] : "");
    }

    // 所有分支都必须释放结果集，否则连接无法复用
    mysql_free_result(res);
    return user;
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    // 1.组装sql语句
    string sql = "update user set state = '" + mysql.escape(user.getState()) +
                 "' where id = " + to_string(user.getId());

    return mysql.update(sql);
}

// 重置用户的状态信息
void UserModel::resetState()
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return;
    }

    // 1.组装sql语句
    const string sql = "update user set state = 'offline' where state = 'online'";
    mysql.update(sql);
}
